//
// pcnet32.c
//
// Copyright (c) 2001 S�ren Thygensen Gjesse. All rights reserved.
//
// PCNet32 network card driver
//

#include <os/krnl.h>

#define offsetof(s,m)   (size_t)&(((s *)0)->m)

/*XXX  Need to get a few more of these in */
#define PCI_DEVICE_PCNET32	0x2000
#define PCI_VENDOR_AMD		0x1022

/* Offsets from base I/O address */
#define PCNET32_WIO_RDP		0x10 /* Register data port */
#define PCNET32_WIO_RAP		0x12 /* Register address port */
#define PCNET32_WIO_RESET	0x14
#define PCNET32_WIO_BDP		0x16

#define PCNET32_DWIO_RDP	0x10
#define PCNET32_DWIO_RAP	0x14
#define PCNET32_DWIO_RESET	0x18
#define PCNET32_DWIO_BDP	0x1C

/* Controller Status Registers */
#define CSR                     0
#define INIT_BLOCK_ADDRESS_LOW  1
#define INIT_BLOCK_ADDRESS_HIGH 2
#define INTERRUPT_MASK          3
#define FEATURE_CONTROL         4

/* Controller Status Register (CSR0) Bits */
#define CSR_ERR                 0x8000
#define CSR_BABL                0x4000
#define CSR_CERR                0x2000
#define CSR_MISS                0x1000
#define CSR_MERR                0x0800
#define CSR_RINT                0x0400
#define CSR_TINT                0x0200
#define CSR_IDON                0x0100
#define CSR_INTR                0x0080
#define CSR_IENA                0x0040
#define CSR_RXON                0x0020
#define CSR_TXON                0x0010
#define CSR_TDMD                0x0008
#define CSR_STOP                0x0004
#define CSR_STRT                0x0002
#define CSR_INIT                0x0001

/* Miscellaneous Configuration (BCR2) */
#define MISCCFG                 2
#define MISCCFG_TMAULOOP        0x4000
#define MISCCFG_APROMWE         0x0100
#define MISCCFG_INTLEVEL        0x0080
#define MISCCFG_DXCVRCTL        0x0020
#define MISCCFG_DXCVRPOL        0x0010
#define MISCCFG_EADISEL         0x0008
#define MISCCFG_AWAKE           0x0004
#define MISCCFG_ASEL            0x0002
#define MISCCFG_XMAUSEL         0x0001
#define MISCCFG_                0x0000
#define MISCCFG_                0x0000

/* Size of Tx and Rx rings */
#ifndef PCNET32_LOG_TX_BUFFERS
#define PCNET32_LOG_TX_BUFFERS 4
#define PCNET32_LOG_RX_BUFFERS 5
#endif

#define TX_RING_SIZE		(1 << (PCNET32_LOG_TX_BUFFERS))
#define TX_RING_MOD_MASK	(TX_RING_SIZE - 1)
#define TX_RING_LEN_BITS	((PCNET32_LOG_TX_BUFFERS) << 12)

#define RX_RING_SIZE		(1 << (PCNET32_LOG_RX_BUFFERS))
#define RX_RING_MOD_MASK	(RX_RING_SIZE - 1)
#define RX_RING_LEN_BITS	((PCNET32_LOG_RX_BUFFERS) << 4)


#pragma pack(push)
#pragma pack(1)

/* Rx and Tx ring descriptors */
struct pcnet32_rx_head
{
  void *buffer;
  short length;
  unsigned short status;
  unsigned long msg_length;
  unsigned long reserved;
};
	
struct pcnet32_tx_head
{
  void *buffer;
  short length;
  unsigned short status;
  unsigned long misc;
  unsigned long reserved;
};

/* initialization block */
struct pcnet32_init_block
{
  unsigned short mode;
  unsigned short tlen_rlen;
  unsigned char phys_addr[6];
  unsigned short reserved;
  unsigned long filter[2];
  /* Receive and transmit ring base, along with extra bits. */    
  unsigned long rx_ring;
  unsigned long tx_ring;
};

#pragma pack(pop)

/* PCnet32 access functions */
struct pcnet32_access
{
  unsigned short (*read_csr)(unsigned short, int);
  void (*write_csr)(unsigned short, int, unsigned short);
  unsigned short (*read_bcr)(unsigned short, int);
  void (*write_bcr)(unsigned short, int, unsigned short);
  unsigned short (*read_rap)(unsigned short);
  void (*write_rap)(unsigned short, unsigned short);
  void (*reset)(unsigned short);
};

struct pcnet32
{
  struct pcnet32_rx_head rx_ring[RX_RING_SIZE];
  struct pcnet32_tx_head tx_ring[TX_RING_SIZE];
  struct pcnet32_init_block init_block;
  void *rx_buffer[RX_RING_SIZE];
  void *tx_buffer[TX_RING_SIZE];

  unsigned long phys_addr;              // Physical address of this structure
  struct netif *netif;                  // Network interface
  struct pcnet32_access *func;

  unsigned short iobase;		// Configured I/O base
  unsigned short irq;		        // Configured IRQ
  unsigned short membase;               // Configured memory base

  unsigned long next_rx;                // Next free receive ring entry
  unsigned long next_tx;                // Next transmit ring entry

  struct dpc dpc;                       // DPC for driver
  int dpc_pending;                      // DPC is queued, but not processed

  struct event rdc;	                // Remote DMA completed event
  struct event ptx;                     // packet transmitted event

  // Access functions
  unsigned short (*read_csr)(unsigned short, int);
  void (*write_csr)(unsigned short, int, unsigned short);
  unsigned short (*read_bcr)(unsigned short, int);
  void (*write_bcr)(unsigned short, int, unsigned short);
  unsigned short (*read_rap)(unsigned short);
  void (*write_rap)(unsigned short, unsigned short);
  void (*reset)(unsigned short);
};

struct pcnet32 pcnet32;

static void dump_csr(unsigned short csr)
{
  kprintf("CRS: ");
  if (csr & CSR_ERR) kprintf(" ERR");
  if (csr & CSR_BABL) kprintf(" BABL");
  if (csr & CSR_CERR) kprintf(" CERR");
  if (csr & CSR_MISS) kprintf(" MISS");
  if (csr & CSR_MERR) kprintf(" MERR");
  if (csr & CSR_RINT) kprintf(" RINT");
  if (csr & CSR_TINT) kprintf(" TINT");
  if (csr & CSR_IDON) kprintf(" IDON");
  if (csr & CSR_INTR) kprintf(" INTR");
  if (csr & CSR_IENA) kprintf(" IENA");
  if (csr & CSR_RXON) kprintf(" RXON");
  if (csr & CSR_TXON) kprintf(" TXON");
  if (csr & CSR_TDMD) kprintf(" TDMD");
  if (csr & CSR_STOP) kprintf(" STOP");
  if (csr & CSR_STRT) kprintf(" STRT");
  if (csr & CSR_INIT) kprintf(" INIT");
  kprintf("\n");
}

static unsigned short pcnet32_wio_read_csr(unsigned short addr, int index)
{
  _outpw((unsigned short) (addr + PCNET32_WIO_RAP), (unsigned short) index);
  return _inpw((unsigned short) (addr + PCNET32_WIO_RDP));
}

static void pcnet32_wio_write_csr(unsigned short addr, int index, unsigned short val)
{
  _outpw((unsigned short) (addr + PCNET32_WIO_RAP), (unsigned short) index);
  _outpw((unsigned short) (addr + PCNET32_WIO_RDP), val);
}

static unsigned short pcnet32_wio_read_bcr(unsigned short addr, int index)
{
  _outpw((unsigned short) (addr + PCNET32_WIO_RAP), (unsigned short) index);
  return _inpw((unsigned short) (addr + PCNET32_WIO_BDP));
}

static void pcnet32_wio_write_bcr(unsigned short addr, int index, unsigned short val)
{
  _outpw((unsigned short) (addr + PCNET32_WIO_RAP), (unsigned short) index);
  _outpw((unsigned short) (addr + PCNET32_WIO_BDP), val);
}

static unsigned short pcnet32_wio_read_rap(unsigned short addr)
{
  return _inpw((unsigned short) (addr + PCNET32_WIO_RAP));
}

static void pcnet32_wio_write_rap(unsigned short addr, unsigned short val)
{
  _outpw((unsigned short) (addr + PCNET32_WIO_RAP), val);
}

static void pcnet32_wio_reset(unsigned short addr)
{
  _inpw((unsigned short) (addr + PCNET32_WIO_RESET));
}

static int pcnet32_wio_check(unsigned short addr)
{
  _outpw((unsigned short) (addr + PCNET32_WIO_RAP), 88);
  return _inpw((unsigned short) (addr + PCNET32_WIO_RAP)) == 88;
}

static struct pcnet32_access pcnet32_wio =
{
  pcnet32_wio_read_csr,
  pcnet32_wio_write_csr,
  pcnet32_wio_read_bcr,
  pcnet32_wio_write_bcr,
  pcnet32_wio_read_rap,
  pcnet32_wio_write_rap,
  pcnet32_wio_reset
};

static unsigned short pcnet32_dwio_read_csr(unsigned short addr, int index)
{
  _outpd((unsigned short) (addr + PCNET32_DWIO_RAP), index);
  return (unsigned short) (_inpd((unsigned short) (addr + PCNET32_DWIO_RDP)) & 0xffff);
}

static void pcnet32_dwio_write_csr (unsigned short addr, int index, unsigned short val)
{
  _outpd((unsigned short) (addr + PCNET32_DWIO_RAP), index);
  _outpd((unsigned short) (addr + PCNET32_DWIO_RDP), val);
}

static unsigned short pcnet32_dwio_read_bcr(unsigned short addr, int index)
{
  _outpd((unsigned short) (addr + PCNET32_DWIO_RAP), index);
  return (unsigned short) _inpd((unsigned short) (addr + PCNET32_DWIO_BDP)) & 0xffff;
}

static void pcnet32_dwio_write_bcr(unsigned short addr, int index, unsigned short val)
{
  _outpd((unsigned short) (addr + PCNET32_DWIO_RAP), index);
  _outpd((unsigned short) (addr + PCNET32_DWIO_BDP), val);
}

static unsigned short pcnet32_dwio_read_rap(unsigned short addr)
{
  return (unsigned short) _inpd((unsigned short) (addr + PCNET32_DWIO_RAP)) & 0xffff;
}

static void pcnet32_dwio_write_rap(unsigned short addr, unsigned short val)
{
  _outpd((unsigned short) (addr + PCNET32_DWIO_RAP), val);
}

static void pcnet32_dwio_reset(unsigned short addr)
{
  _inpd((unsigned short) (addr + PCNET32_DWIO_RESET));
}

static int pcnet32_dwio_check(unsigned short addr)
{
  _outpd((unsigned short) (addr + PCNET32_DWIO_RAP), 88);
  return _inpd((unsigned short) (addr + PCNET32_DWIO_RAP)) == 88;
}

static struct pcnet32_access pcnet32_dwio =
{
  pcnet32_dwio_read_csr,
  pcnet32_dwio_write_csr,
  pcnet32_dwio_read_bcr,
  pcnet32_dwio_write_bcr,
  pcnet32_dwio_read_rap,
  pcnet32_dwio_write_rap,
  pcnet32_dwio_reset

};

err_t pcnet32_transmit(struct netif *netif, struct pbuf *p)
{
  unsigned char *data;
  int len;
  struct pbuf *q;
  struct pcnet32 *pcnet32 = netif->state;
  int entry = pcnet32->next_tx;
  unsigned short status = 0x8300;

  kprintf("pcnet32_transmit: transmit packet len=%d\n", p->tot_len);
  p->ref++;
  for (q = p; q != NULL; q = q->next) 
  {
    len = q->len;
    if (len > 0)
    {
      kprintf("pcnet32_transmit: sending %d (entry = %d)\n", len, entry);
      data = q->payload;

      pcnet32->tx_buffer[entry] = q;
      pcnet32->tx_ring[entry].buffer = virt2phys(data);
      pcnet32->tx_ring[entry].length = -len;
      pcnet32->tx_ring[entry].misc = 0x00000000;
      pcnet32->tx_ring[entry].status = status;

      // Move to next entry
      entry = (++entry) & TX_RING_MOD_MASK;
      pcnet32->next_tx = entry;
    }
    else
    {
      kprintf("pcnet32_transmit: enpty pbuf\n");
    }

    // Trigger an immediate send poll
    pcnet32->write_csr(pcnet32->iobase, CSR, CSR_IENA | CSR_TDMD);
  }

  return 0;
}

void pcnet32_receive(struct pcnet32 *pcnet32)
{
  int entry = pcnet32->next_rx;
  struct pbuf *p, *q;

  kprintf("Receive entry %d, 0x%04X\n", entry, pcnet32->rx_ring[entry].status);
  while (!(pcnet32->rx_ring[entry].status & 0x8000))
  {
    int status = pcnet32->rx_ring[entry].status >> 8;
    kprintf("Receive entry %d, %d\n", entry, status);
    if (status != 0x03)
    {
      // Error
    }
    else
    {
      char *packet_ptr;
      short pkt_len = (short) (pcnet32->rx_ring[entry].msg_length & 0xfff)-4;
      kprintf("length %d\n", pkt_len);

      // Allocate packet buffer
      p = pbuf_alloc(PBUF_LINK, pkt_len, PBUF_POOL);

      // Get packet from nic and send to upper layer
      if (p != NULL)
      {
	packet_ptr = (char *) pcnet32->rx_buffer[entry];
	for (q = p; q != NULL; q = q->next) 
	{
	  //ne_get_packet(packet_ptr, q->payload, (unsigned short) q->len);
	  memcpy(q->payload, packet_ptr, q->len);
	  packet_ptr += q->len;
	}

	pcnet32->netif->ethinput(p, pcnet32->netif);
      }
      else
      {
	// Drop packet
	kprintf("drop\n");
	stats.link.memerr++;
	stats.link.drop++;
      }

      // give ownership back to card
      pcnet32->rx_ring[entry].length = -1544; // Note 1
      pcnet32->rx_ring[entry].status |= 0x8000;

      // Mode to next entry
      entry = (++entry) & RX_RING_MOD_MASK;
      pcnet32->next_rx = entry;
    }
  }
}

void pcnet32_dpc(void *arg)
{
  struct pcnet32 *pcnet32 = (struct pcnet32 *) arg;
  unsigned short iobase = pcnet32->iobase;
  unsigned short csr;

  // Mark DPC as ready
  pcnet32->dpc_pending = 0;

  while ((csr = pcnet32->read_csr(iobase, CSR)) & (CSR_ERR | CSR_RINT | CSR_TINT))
  {
    // Acknowledge all of the current interrupt sources
    pcnet32->write_csr(iobase, CSR, (unsigned short) (csr & ~(CSR_IENA | CSR_TDMD | CSR_STOP | CSR_STRT | CSR_INIT)));

    dump_csr(csr);

    if (csr & CSR_RINT) pcnet32_receive(pcnet32);
    if (csr & CSR_TINT) kprintf("Transmit\n");
  }
  dump_csr(csr);

  //kprintf("pcnet32: intr (csr0 = %08x)\n", pcnet32->func->read_csr(pcnet32->iobase, 0));
  //dump_csr(pcnet32->func->read_csr(pcnet32->iobase, 0));



  
  pcnet32->write_csr(iobase, CSR, CSR_BABL | CSR_CERR | CSR_MISS | CSR_MERR | CSR_IDON | CSR_IENA);

  eoi(pcnet32->irq);
}

void pcnet32_handler(struct context *ctxt, void *arg)
{
  struct pcnet32 *pcnet32 = (struct pcnet32 *) arg;
  kprintf("pcnet32: intr\n");

  // Queue DPC to service interrupt
  if (!pcnet32->dpc_pending)
  {
    pcnet32->dpc_pending = 1;
    queue_irq_dpc(&pcnet32->dpc, pcnet32_dpc, pcnet32);
  }
  else
    kprintf("pcnet32: intr lost\n");
}

int init_pcnet32(struct netif *netif)
{
  int version;
  char *chipname;
  struct pcnet32_access *func;
  int i;
  char str[20];
  unsigned short val;
  unsigned long init_block;
  unsigned long value;
  struct pci_dev *dev;

  // Initialize network interface
  pcnet32.netif = netif;
  pcnet32.netif->state = &pcnet32;
  pcnet32.netif->ethoutput = pcnet32_transmit;

  pcnet32.phys_addr = (unsigned long) virt2phys(&pcnet32);
  dev = lookup_pci_device(PCI_VENDOR_AMD, PCI_DEVICE_PCNET32);
  if (dev)
  {
    // Setup NIC configuration
    pcnet32.iobase = (unsigned short) dev->iobase;
    pcnet32.irq = (unsigned short) dev->irq;
    pcnet32.membase = (unsigned short) dev->membase;

    kprintf("PCnet32: iobase 0x%x irq %d\n", pcnet32.iobase, pcnet32.irq);

    /* Enable bus mastering */
    value = pci_config_read(dev->bus->busno, dev->devno, dev->funcno, PCI_CONFIG_CMD_STAT);
    value |= 0x00000004;
    pci_config_write(dev->bus->busno, dev->devno, dev->funcno, PCI_CONFIG_CMD_STAT, value);
    
    /* Reset the chip */
    pcnet32_dwio_reset(pcnet32.iobase);
    pcnet32_wio_reset(pcnet32.iobase);

    if (pcnet32_wio_read_csr(pcnet32.iobase, 0) == 4 && pcnet32_wio_check(pcnet32.iobase))
    {
      func = &pcnet32_wio;
      pcnet32.func = &pcnet32_wio;
    }
    else
    {
      if (pcnet32_dwio_read_csr(pcnet32.iobase, 0) == 4 && pcnet32_dwio_check(pcnet32.iobase))
      {
        func = &pcnet32_dwio;
	pcnet32.func = &pcnet32_dwio;
      }
      else
      {
	return 0;
      }
    }

    // Setup access functions
    pcnet32.read_csr = func->read_csr;
    pcnet32.write_csr = func->write_csr;
    pcnet32.read_bcr = func->read_bcr;
    pcnet32.write_bcr = func->write_bcr;
    pcnet32.read_rap = func->read_rap;
    pcnet32.write_rap = func->write_rap;
    pcnet32.reset = func->reset;

    version = pcnet32.func->read_csr(pcnet32.iobase, 88) | (pcnet32_wio_read_csr(pcnet32.iobase, 89) << 16);
    kprintf("PCnet chip version is %#x.\n", version);
    if ((version & 0xfff) != 0x003) return 0;
    version = (version >> 12) & 0xffff;
    switch (version)
    {
      case 0x2420:
        chipname = "PCnet/PCI 79C970";
        break;
      case 0x2430:
        chipname = "PCnet/PCI 79C970";
        break;
      case 0x2621:
        chipname = "PCnet/PCI II 79C970A";
	//fdx = 1;
	break;
      case 0x2623:
	chipname = "PCnet/FAST 79C971";
	//fdx = 1; mii = 1; fset = 1;
	//ltint = 1;
	break;
      case 0x2624:
	chipname = "PCnet/FAST+ 79C972";
	//fdx = 1; mii = 1; fset = 1;
	break;
      case 0x2625:
	chipname = "PCnet/FAST III 79C973";
	//fdx = 1; mii = 1;
	break;
      case 0x2626:
	chipname = "PCnet/Home 79C978";
	//fdx = 1;
	break;
      case 0x2627:
	chipname = "PCnet/FAST III 79C975";
	//fdx = 1; mii = 1;
	break;
      default:
	kprintf("pcnet32: PCnet version %#x, no PCnet32 chip.\n", version);
	return 0;
    }

    // Install interrupt handler
    pcnet32.dpc_pending = 0;
    set_interrupt_handler(IRQ2INTR(pcnet32.irq), pcnet32_handler, &pcnet32);
    enable_irq(pcnet32.irq);

    // Read MAC address from PROM
    for (i = 0; i < ETHER_ADDR_LEN; i++) pcnet32.netif->hwaddr.addr[i] = (unsigned char) _inp((unsigned short) (pcnet32.iobase + i));

    /* Setup the init block */
    //pcnet32.init_block.mode = 0x0003;
    //pcnet32.init_block.mode = 0x8000;
    //pcnet32.init_block.mode = 0x0000;
    pcnet32.init_block.mode = 0x0080; // ASEL
    pcnet32.init_block.tlen_rlen = TX_RING_LEN_BITS | RX_RING_LEN_BITS;
    for (i = 0; i < 6; i++) pcnet32.init_block.phys_addr[i] = pcnet32.netif->hwaddr.addr[i];
    pcnet32.init_block.filter[0] = 0x00000000;
    pcnet32.init_block.filter[1] = 0x00000000;
    pcnet32.init_block.rx_ring = pcnet32.phys_addr + offsetof(struct pcnet32, rx_ring);
    pcnet32.init_block.tx_ring = pcnet32.phys_addr + offsetof(struct pcnet32, tx_ring);

    // Allocate receive ring
    for (i = 0; i < RX_RING_SIZE; i++)
    {
      pcnet32.rx_buffer[i] = kmalloc(1544);
      pcnet32.rx_ring[i].buffer = virt2phys(pcnet32.rx_buffer[i]);
      pcnet32.rx_ring[i].length = (short) -1544;
      pcnet32.rx_ring[i].status = 0x8000;
    }
    pcnet32.next_rx = 0;

    // Initialize transmit ring
    for (i = 0; i < TX_RING_SIZE; i++)
    {
      pcnet32.tx_buffer[i] = NULL;
      pcnet32.tx_ring[i].buffer = NULL;
      pcnet32.tx_ring[i].status = 0;
    }
    pcnet32.next_tx = 0;

    /* reset pcnet32 */
    pcnet32.func->reset(pcnet32.iobase);
    
    /* switch pcnet32 to 32bit mode */
    pcnet32.func->write_bcr(pcnet32.iobase, 20, 2);

    /* set autoselect bit */
    //val = pcnet32.func->read_bcr(pcnet32.iobase, MISCCFG) & ~MISCCFG_ASEL;
    //val |= MISCCFG_ASEL;
    //pcnet32.func->write_bcr(pcnet32.iobase, MISCCFG, val);

    /* set full duplex */
    val = pcnet32.func->read_bcr(pcnet32.iobase, 9) & ~3;
    val |= 1;
    pcnet32.func->write_bcr(pcnet32.iobase, 9, val);

    init_block = pcnet32.phys_addr + offsetof(struct pcnet32, init_block);
    kprintf("init_block %08X\n", init_block);
    kprintf("rx_ring %08X\n", pcnet32.init_block.rx_ring);
    kprintf("tx_ring %08X\n", pcnet32.init_block.tx_ring);
    pcnet32.func->write_csr(pcnet32.iobase, 1, (unsigned short) (init_block & 0xffff));
    pcnet32.func->write_csr(pcnet32.iobase, 2, (unsigned short) (init_block >> 16));

    pcnet32.func->write_csr(pcnet32.iobase, 4, 0x0915);
    pcnet32.func->write_csr(pcnet32.iobase, 0, CSR_INIT);

    i = 0;
    while (i++ < 100)
	if (pcnet32.func->read_csr(pcnet32.iobase, 0) & CSR_IDON)
	{
	   kprintf("XXXXXXXXXXXXXXXXXXXXXXX %d\n", i);
	    break;
	}
    /* 
     * We used to clear the InitDone bit, 0x0100, here but Mark Stockton
     * reports that doing so triggers a bug in the '974.
     */
    pcnet32.func->write_csr(pcnet32.iobase, 0, CSR_IENA | CSR_STRT);
    kprintf("XXXX: %d", sizeof(struct pcnet32_init_block));

	//printk(KERN_DEBUG "%s: pcnet32 open after %d ticks, init block %#x csr0 %4.4x.\n",
	//       dev->name, i, (u32) (lp->dma_addr + offsetof(struct pcnet32_private, init_block)),
	//       lp->a.read_csr (ioaddr, 0));

    kprintf("PCnet32: iobase 0x%x irq %d hwaddr %s chipname %s\n", pcnet32.iobase, pcnet32.irq, ether2str(&pcnet32.netif->hwaddr, str), chipname);
  }

  return 1;
}

// Note 1

/*
 * The docs say that the buffer length isn't touched, but Andrew Boyd
 * of QNX reports that some revs of the 79C965 clear it.
 */
