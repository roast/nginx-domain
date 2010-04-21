
/*
* Copyright 2009 Roast
*/

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

typedef struct {
	ngx_flag_t	enable;
	ngx_str_t	type;
	ngx_array_t *stop_word;
} ngx_http_domain_conf_t;

static void *ngx_http_domain_create_conf(ngx_conf_t *cf);
static char *ngx_http_domain_merge_conf(ngx_conf_t *cf, void *parent, void *child);
static ngx_int_t ngx_http_domain_init(ngx_conf_t *cf);
static char *ngx_http_domain_stop(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);

static ngx_command_t ngx_http_domain_commands[] = {
	{ngx_string("domain"),
	NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_FLAG,
	ngx_conf_set_flag_slot,
	NGX_HTTP_LOC_CONF_OFFSET,
	offsetof(ngx_http_domain_conf_t, enable),
	NULL},

	{ngx_string("domain_type"),
	NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_FLAG,
	ngx_conf_set_str_slot,
	NGX_HTTP_LOC_CONF_OFFSET,
	offsetof(ngx_http_domain_conf_t, type),
	NULL},

	{ ngx_string("domain_stop"),
	NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_1MORE,
	ngx_http_domain_stop,
	NGX_HTTP_LOC_CONF_OFFSET,
	0,
	NULL },

	ngx_null_command
};

static ngx_http_module_t ngx_http_domain_module_ctx = {
	NULL,                         /* preconfiguration */
	ngx_http_domain_init,   /* postconfiguration */

	NULL,                         /* create main configuration */
	NULL,                         /* init main configuration */

	NULL,                         /* create server configuration */
	NULL,                         /* merge server configuration */

	ngx_http_domain_create_conf,   /* create location configuration */
	ngx_http_domain_merge_conf     /* merge location configuration */
};

ngx_module_t  ngx_http_domain_module = {
	NGX_MODULE_V1,
	&ngx_http_domain_module_ctx,	   /* module context */
	ngx_http_domain_commands,		   /* module directives */
	NGX_HTTP_MODULE,                       /* module type */
	NULL,                                  /* init master */
	NULL,                                  /* init module */
	NULL,                                  /* init process */
	NULL,                                  /* init thread */
	NULL,                                  /* exit thread */
	NULL,                                  /* exit process */
	NULL,                                  /* exit master */
	NGX_MODULE_V1_PADDING
};


static ngx_int_t
ngx_http_domain_handler(ngx_http_request_t *r)
{
	u_char	*s;
	ngx_str_t	*uri, *word;
	ngx_uint_t	i = 1, m = 0, x = 0;
	ngx_table_elt_t  *h;
	ngx_http_domain_conf_t	*xucf;
	
	xucf = ngx_http_get_module_loc_conf(r, ngx_http_domain_module);

	if (!xucf->enable) {
		return NGX_DECLINED;
	}

	//ngx_log_error(NGX_LOG_ALERT, r->connection->log, 0, "domain:uri:%V", &(r->uri));

	uri = ngx_pcalloc(r->pool, sizeof(ngx_str_t));
	if (uri== NULL) {
		ngx_log_error(NGX_LOG_ALERT, r->connection->log, 0, "domain:init tmp uri failed");
		return NGX_ERROR;
	}

	uri->len = 256;
	uri->data = ngx_pcalloc(r->pool, uri->len);
	if (uri->data == NULL) {
		ngx_log_error(NGX_LOG_ALERT, r->connection->log, 0, "domain:init tmp uri data failed");
		return NGX_ERROR;
	}

	for (; i< r->uri.len; i++)
	{
		if (r->uri.data[i] == '/' && m == 0)
			m = i;

		if (r->uri.data[i] == '.')
		{
			/* file, not domain */
			if (m == 0)
				return NGX_DECLINED;
			else
			{
				x = 1;
				break;
			}
		}
	}

	//ngx_log_error(NGX_LOG_ALERT, r->connection->log, 0, "domain:m:%d", m);

	/* no uri*/
	if (m == 0)
		m = r->uri.len;

	/* check stop word */
	if (xucf->stop_word) 
	{
		word = xucf->stop_word->elts;

		for (i = 0; i < xucf->stop_word->nelts; i++) 
		{
			//ngx_log_error(NGX_LOG_ALERT, r->connection->log, 0, "domain:word %s m %d uri %s", word[i].data, m-1, r->uri.data);

			if ((m-1) == word[i].len && ngx_strncmp((r->uri.data + 1), word[i].data, (m-1)) == 0)
			{
				return NGX_DECLINED;
			}
		}
	}

	/* redirect:../domain/subdir  ../domain/subdir/ */
	if (r->uri.data[r->uri.len - 1] == '/' || (m != r->uri.len && x == 0))
	{
		//ngx_log_error(NGX_LOG_ALERT, r->connection->log, 0, "domain:redirect %V", &r->uri);

		ngx_memcpy(uri->data, r->uri.data, r->uri.len);
		uri->len = r->uri.len;

		if (r->uri.data[r->uri.len - 1] == '/')
		{
			ngx_memcpy((uri->data+uri->len), "index.php", 9);
			uri->len = uri->len + 10;
		}
		else
		{
			ngx_memcpy((uri->data+uri->len), "/index.php", 10);
			uri->len = uri->len + 11;
		}
		
		r->header_only = 1;
		r->keepalive = 0;
		r->headers_out.status = 302;

		ngx_table_elt_t *header = ngx_list_push(&r->headers_out.headers);
		if (header == NULL) {
			return NGX_HTTP_INTERNAL_SERVER_ERROR;
		}

		header->hash = 1;
		header->key.len = sizeof("Location") - 1;
		header->key.data = (u_char *) "Location";
		header->value.len = uri->len - 1;
		header->value.data = uri->data;

		return NGX_HTTP_MOVED_TEMPORARILY;
	}

	s = r->uri.data+m;

	/* if app, not append domain type to url */
	if (s[0] == '/' && s[1] == 'a' && s[2] == 'p' && s[3] == 'p' && s[4] == '/')
		i = 0;
	else
	{
		uri->data[0] = '/';

		ngx_memcpy((uri->data+1), xucf->type.data, xucf->type.len);
		i = xucf->type.len + 1;
	}
	
	/* change request uri, not process "/" */
	if (m < r->uri.len)
	{
		ngx_memcpy((uri->data+i), (r->uri.data+m), (r->uri.len-m));
		uri->len = i + r->uri.len - m;
	}
	else
	{
		ngx_memcpy((uri->data+i), "/index.php", 10);
		uri->len = i + 10;
	}

	//ngx_log_error(NGX_LOG_ALERT, r->connection->log, 0, "domain:new uri:%s", uri->data);

	/* add domain header */
	h = ngx_list_push(&r->headers_in.headers);
	if (h == NULL) {
		return NGX_ERROR;
	}

	h->hash = 1;
	h->key.len = 6;
	h->key.data = (u_char *)"DOMAIN";
	h->value.len = (m-1);
	h->value.data = (r->uri.data + 1);

	r->uri.len = uri->len;
	r->uri.data = uri->data;

	return NGX_DECLINED;	
}

static char *
ngx_http_domain_stop(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
	ngx_http_domain_conf_t *dcf = conf;

	ngx_str_t   *value, *word;
	ngx_uint_t   i;

	value = cf->args->elts;

	for (i = 1; i < cf->args->nelts; i++) 
	{
		if (dcf->stop_word == NULL) 
		{
			dcf->stop_word = ngx_array_create(cf->pool, 20, sizeof(ngx_str_t));
			if (dcf->stop_word == NULL) 
			{
				return NGX_CONF_ERROR;
			}
		}

		word = ngx_array_push(dcf->stop_word);
		if (word == NULL) 
		{
			return NGX_CONF_ERROR;
		}

		*word = value[i];
	}

	return NGX_CONF_OK;
}

static ngx_int_t
ngx_http_domain_init(ngx_conf_t *cf)
{
	ngx_http_handler_pt        *h;
	ngx_http_core_main_conf_t  *cmcf;

	cmcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_core_module);

	h = ngx_array_push(&cmcf->phases[NGX_HTTP_POST_READ_PHASE].handlers);
	if (h == NULL) {
		return NGX_ERROR;
	}

	*h = ngx_http_domain_handler;

	return NGX_OK;
}

static void *
ngx_http_domain_create_conf(ngx_conf_t *cf) 
{
	ngx_http_domain_conf_t  *conf;

	conf = ngx_pcalloc(cf->pool, sizeof(ngx_http_domain_conf_t));
	if (conf == NULL) {
		return NGX_CONF_ERROR;
	}

	conf->enable = NGX_CONF_UNSET;

	return conf;
}

static char *
ngx_http_domain_merge_conf(ngx_conf_t *cf, void *parent, void *child) 
{
	ngx_http_domain_conf_t  *prev = parent;
	ngx_http_domain_conf_t  *conf = child;

	ngx_conf_merge_value(conf->enable, prev->enable, 0);
	ngx_conf_merge_str_value(conf->type, prev->type, "space");

	if (conf->stop_word == NULL) {
		conf->stop_word = prev->stop_word;
	}

	return NGX_CONF_OK;
}
