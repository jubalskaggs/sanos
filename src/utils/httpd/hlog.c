//
// hlog.c
//
// HTTP Logging
//
// Copyright (C) 2002 Michael Ringgaard. All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions
// are met:
// 
// 1. Redistributions of source code must retain the above copyright 
//    notice, this list of conditions and the following disclaimer.  
// 2. Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the
//    documentation and/or other materials provided with the distribution.  
// 3. Neither the name of the project nor the names of its contributors
//    may be used to endorse or promote products derived from this software
//    without specific prior written permission. 
// 
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
// ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE
// FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
// DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
// OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
// HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
// LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
// OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF 
// SUCH DAMAGE.
// 

#include <os.h>
#include <string.h>
#include <time.h>

#include <httpd.h>

#define MAX_LOGLINE_SIZE 32*K

char *logfieldnames[] = 
{
  "date",
  "time",
  "time-taken",
  "c-ip",
  "s-ip",
  "s-port",
  "cs-uri",
  "cs-uri-stem",
  "cs-uri-query",
  "cs-method",
  "cs-bytes",
  "cs(user-agent)",
  "cs(referer)",
  "cs(cookie)",
  "cs(host)",
  "cs-username",
  "cs-protocol",
  "sc-bytes",
  "sc-status",
  NULL
};

int parse_log_columns(struct httpd_server *server, char *fields)
{
  char *p;
  char *q;
  int n;
  char str[128];

  server->nlogcolumns = 0;
  p = fields;
  if (!p) return 0;

  while (*p && server->nlogcolumns < HTTP_NLOGCOLUMNS - 1)
  {
    while (*p == ' ') p++;
    q = str;
    while (*p && *p != ' ') *q++ = *p++;
    *q = 0;

    n = 0;
    while (logfieldnames[n] && stricmp(str, logfieldnames[n]) != 0) n++;
    if (logfieldnames[n]) server->logcoumns[server->nlogcolumns++] = n;
  }

  return 0;
}

int log_request(struct httpd_request *req)
{
  char line[MAX_LOGLINE_SIZE];
  char *p = line;
  char *end = line + MAX_LOGLINE_SIZE;
  struct httpd_connection *conn = req->conn;
  struct httpd_response *rsp = conn->rsp;
  time_t now = time(0);
  struct tm *tm = gmtime(&now);
  char buf[32];
  char *value;
  int n;
  int field;

  if (req->conn->server->nlogcolumns == 0) return 0;

  for (n = 0; n < req->conn->server->nlogcolumns; n++)
  {
    field = req->conn->server->logcoumns[n];
    value = buf;

    switch (field)
    {
      case HTTP_LOG_DATE: 
	strftime(buf, sizeof(buf), "%Y-%m-%d", tm);
	break;

      case HTTP_LOG_TIME:
	strftime(buf, sizeof(buf), "%H:%M:%S", tm);
	break;

      case HTTP_LOG_TIME_TAKEN:
	value = "0";
	break;

      case HTTP_LOG_C_IP:
	strcpy(buf, inet_ntoa(conn->client_addr.sa_in.sin_addr));
	break;

      case HTTP_LOG_S_IP:
	strcpy(buf, inet_ntoa(conn->server_addr.sa_in.sin_addr));
	break;

      case HTTP_LOG_S_PORT:
	sprintf(buf, "%d", ntohs(conn->server_addr.sa_in.sin_port));
	break;

      case HTTP_LOG_CS_URI:
      case HTTP_LOG_CS_URI_STEM:
	value = req->decoded_url;
	break;

      case HTTP_LOG_CS_URI_QUERY:
	value = req->query;
	break;

      case HTTP_LOG_CS_METHOD:
	value = req->method;
	break;

      case HTTP_LOG_CS_BYTES:
	if (req->content_length > 0)
	  sprintf(buf, "%d", req->content_length);
	else
	  value = NULL;
	break;
	
      case HTTP_LOG_CS_USER_AGENT:
	value = req->user_agent;
	break;

      case HTTP_LOG_CS_REFERER:
	value = req->referer;
	break;

      case HTTP_LOG_CS_COOKIE:
	value = req->cookie;
	break;

      case HTTP_LOG_CS_HOST:
	value = req->host;
	break;

      case HTTP_LOG_CS_USERNAME:
	value = req->username;
	break;

      case HTTP_LOG_CS_PROTOCOL:
	value = req->protocol;
	break;

      case HTTP_LOG_SC_BYTES:
	if (rsp->content_length > 0)
	  sprintf(buf, "%d", rsp->content_length);
	else
	  value = NULL;
	break;

      case HTTP_LOG_SC_STATUS:
	sprintf(buf, "%d", rsp->status);
	break;
    }

    if (!value || !*value) value = "-";
    if (n > 0)
    {
      if (p == end) return -EBUF;
      *p++ = ' ';
    }
    while (*value)
    {
      if (p == end) return -EBUF;
      if (*value == ' ')
	*p++ = '+';
      else
	*p++ = *value;

      value++;
    }
  }

  if (p == end) return -EBUF;
  *p++ = '\r';
  if (p == end) return -EBUF;
  *p++ = '\n';
  if (p == end) return -EBUF;
  *p++ = 0;

  write(conn->server->logfd, line, p - line);

  return p - line;
}
