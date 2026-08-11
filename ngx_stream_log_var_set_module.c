
/*
 * Copyright (C) Hanada
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_stream.h>

#if (NGX_CONDITION)
#include <ngx_stream_condition_module.h>
#endif


typedef struct {
    ngx_int_t                    index;
    ngx_stream_complex_value_t   value;
    ngx_stream_set_variable_pt   set_handler;
#if (NGX_CONDITION)
    ngx_condition_expr_id_t      expr_id;
#else
    ngx_stream_complex_value_t  *filter;
    ngx_int_t                    negative;
#endif
} ngx_stream_log_var_set_variable_t;


typedef struct {
    ngx_array_t                 *vars;
} ngx_stream_log_var_set_srv_conf_t;


static ngx_int_t ngx_stream_log_var_set_handler(ngx_stream_session_t *s);
static char *ngx_stream_log_var_set(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);
static ngx_int_t ngx_stream_log_var_set_variable(ngx_stream_session_t *s,
    ngx_stream_variable_value_t *v, uintptr_t data);
static void *ngx_stream_log_var_set_create_srv_conf(ngx_conf_t *cf);
static char *ngx_stream_log_var_set_merge_srv_conf(ngx_conf_t *cf,
    void *parent, void *child);
static ngx_int_t ngx_stream_log_var_set_init(ngx_conf_t *cf);


static ngx_command_t  ngx_stream_log_var_set_commands[] = {

    { ngx_string("log_var_set"),
      NGX_STREAM_MAIN_CONF|NGX_STREAM_SRV_CONF
#if (NGX_CONDITION)
                           |NGX_STREAM_MAIN_WHEN_CONF
                           |NGX_STREAM_SRV_WHEN_CONF|NGX_CONF_TAKE2,
#else
                           |NGX_CONF_TAKE23,
#endif
      ngx_stream_log_var_set,
      NGX_STREAM_SRV_CONF_OFFSET,
      0,
      NULL },

      ngx_null_command
};


static ngx_stream_module_t  ngx_stream_log_var_set_module_ctx = {
    NULL,                                   /* preconfiguration */
    ngx_stream_log_var_set_init,            /* postconfiguration */

    NULL,                                   /* create main conf */
    NULL,                                   /* init main conf */

    ngx_stream_log_var_set_create_srv_conf, /* create srv conf */
    ngx_stream_log_var_set_merge_srv_conf   /* merge srv conf */
};


ngx_module_t  ngx_stream_log_var_set_module = {
    NGX_MODULE_V1,
    &ngx_stream_log_var_set_module_ctx,     /* module context */
    ngx_stream_log_var_set_commands,        /* module directives */
    NGX_STREAM_MODULE,                      /* module type */
    NULL,                                   /* init master */
    NULL,                                   /* init module */
    NULL,                                   /* init process */
    NULL,                                   /* init thread */
    NULL,                                   /* exit thread */
    NULL,                                   /* exit process */
    NULL,                                   /* exit master */
    NGX_MODULE_V1_PADDING
};


static ngx_int_t
ngx_stream_log_var_set_handler(ngx_stream_session_t *s)
{
    ngx_str_t                            val;
    ngx_stream_variable_t               *v;
    ngx_stream_variable_value_t         *vv;
    ngx_stream_log_var_set_srv_conf_t   *lscf;
    ngx_stream_log_var_set_variable_t   *lv, *last;
    ngx_stream_core_main_conf_t         *cmcf;

    ngx_log_debug0(NGX_LOG_DEBUG_STREAM, s->connection->log, 0,
                   "log var set handler");

    lscf = ngx_stream_get_module_srv_conf(s, ngx_stream_log_var_set_module);

    if (lscf->vars == NULL) {
        return NGX_OK;
    }

    cmcf = ngx_stream_get_module_main_conf(s, ngx_stream_core_module);
    v = cmcf->variables.elts;

    lv = lscf->vars->elts;
    last = lv + lscf->vars->nelts;

    while (lv < last) {

#if (NGX_CONDITION)
        if (ngx_stream_condition_get_expr_result(s, lv->expr_id)
            != NGX_CONDITION_EXPR_HIT)
        {
            lv++;
            continue;
        }
#else
        if (lv->filter) {
            if (ngx_stream_complex_value(s, lv->filter, &val)
                != NGX_OK)
            {
                return NGX_ERROR;
            }

            if (val.len == 0 || (val.len == 1 && val.data[0] == '0')) {
                if (!lv->negative) {
                    lv++;
                    continue;
                }

            } else {
                if (lv->negative) {
                    lv++;
                    continue;
                }
            }
        }
#endif

        /*
         * explicitly set new value to make sure it will be available after
         * internal redirects
         */

        vv = &s->variables[lv->index];

        if (ngx_stream_complex_value(s, &lv->value, &val) != NGX_OK) {
            return NGX_ERROR;
        }

        vv->valid = 1;
        vv->not_found = 0;
        vv->data = val.data;
        vv->len = val.len;

        if (lv->set_handler) {
            /*
             * set_handler only available in cmcf->variables_keys, so we store
             * it explicitly
             */

            lv->set_handler(s, vv, v[lv->index].data);
        }

        lv++;
    }

    return NGX_OK;
}


static char *
ngx_stream_log_var_set(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_stream_log_var_set_srv_conf_t  *lscf = conf;

    ngx_str_t                           *value;
    ngx_stream_variable_t               *v;
    ngx_stream_log_var_set_variable_t   *lv;
    ngx_str_t                            s;
    ngx_stream_compile_complex_value_t   ccv;

    value = cf->args->elts;

    if (value[1].data[0] != '$') {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "invalid variable name \"%V\"", &value[1]);
        return NGX_CONF_ERROR;
    }

    value[1].len--;
    value[1].data++;

    if (lscf->vars == NGX_CONF_UNSET_PTR) {
        lscf->vars = ngx_array_create(
            cf->pool, 1, sizeof(ngx_stream_log_var_set_variable_t));
        if (lscf->vars == NULL) {
            return NGX_CONF_ERROR;
        }
    }

    lv = ngx_array_push(lscf->vars);
    if (lv == NULL) {
        return NGX_CONF_ERROR;
    }

#if (NGX_CONDITION)
    lv->expr_id = ngx_condition_get_associated_expr_id(cf);
#endif

    v = ngx_stream_add_variable(cf, &value[1], NGX_STREAM_VAR_CHANGEABLE);
    if (v == NULL) {
        return NGX_CONF_ERROR;
    }

    lv->index = ngx_stream_get_variable_index(cf, &value[1]);
    if (lv->index == NGX_ERROR) {
        return NGX_CONF_ERROR;
    }

    if (v->get_handler == NULL) {
        v->get_handler = ngx_stream_log_var_set_variable;
        v->data = (uintptr_t) lv;
    }

    lv->set_handler = v->set_handler;

    ngx_memzero(&ccv, sizeof(ngx_stream_compile_complex_value_t));

    ccv.cf = cf;
    ccv.value = &value[2];
    ccv.complex_value = &lv->value;

    if (ngx_stream_compile_complex_value(&ccv) != NGX_OK) {
        return NGX_CONF_ERROR;
    }

#if !(NGX_CONDITION)
    if (cf->args->nelts == 4) {

        if (ngx_strncmp(value[3].data, "if=", 3) == 0) {
            s.len = value[3].len - 3;
            s.data = value[3].data + 3;
            lv->negative = 0;

        } else if (ngx_strncmp(value[3].data, "if!=", 4) == 0) {
            s.len = value[3].len - 4;
            s.data = value[3].data + 4;
            lv->negative = 1;

        } else {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                               "invalid parameter \"%V\"", &value[3]);
            return NGX_CONF_ERROR;
        }

        ngx_memzero(&ccv, sizeof(ngx_stream_compile_complex_value_t));

        ccv.cf = cf;
        ccv.value = &s;
        ccv.complex_value = ngx_palloc(cf->pool,
                                       sizeof(ngx_stream_complex_value_t));
        if (ccv.complex_value == NULL) {
            return NGX_CONF_ERROR;
        }

        if (ngx_stream_compile_complex_value(&ccv) != NGX_OK) {
            return NGX_CONF_ERROR;
        }

        lv->filter = ccv.complex_value;

    } else {
        lv->negative = 0;
        lv->filter = NULL;
    }
#endif

    return NGX_CONF_OK;
}


static ngx_int_t
ngx_stream_log_var_set_variable(ngx_stream_session_t *s,
    ngx_stream_variable_value_t *v, uintptr_t data)
{
    ngx_log_debug0(NGX_LOG_DEBUG_STREAM, s->connection->log, 0,
                   "log var set variable");

    v->not_found = 1;

    return NGX_OK;
}


static void *
ngx_stream_log_var_set_create_srv_conf(ngx_conf_t *cf)
{
    ngx_stream_log_var_set_srv_conf_t *conf;

    conf = ngx_pcalloc(cf->pool, sizeof(ngx_stream_log_var_set_srv_conf_t));
    if (conf == NULL) {
        return NULL;
    }

    conf->vars = NGX_CONF_UNSET_PTR;

    return conf;
}


static char *
ngx_stream_log_var_set_merge_srv_conf(ngx_conf_t *cf, void *parent, void *child)
{
    ngx_stream_log_var_set_srv_conf_t  *prev = parent;
    ngx_stream_log_var_set_srv_conf_t  *conf = child;

    ngx_stream_log_var_set_variable_t  *pvars, *cvars, *nvar;
    ngx_uint_t                          i, j, found;
    ngx_uint_t                          cvars_nelts;

    if (conf->vars == NGX_CONF_UNSET_PTR) {
        conf->vars = (prev->vars == NGX_CONF_UNSET_PTR) ? NULL : prev->vars;
        return NGX_CONF_OK;
    }

    if (prev->vars == NGX_CONF_UNSET_PTR || prev->vars == NULL) {
        return NGX_CONF_OK;
    }

    pvars = prev->vars->elts;
    cvars_nelts = conf->vars->nelts;
    for (i = 0; i < prev->vars->nelts; i++) {
        cvars = conf->vars->elts;
        found = 0;

        for (j = 0; j < cvars_nelts; j++) {
            if (cvars[j].index == pvars[i].index) {
                found = 1;
                break;
            }
        }

        if (!found) {
            nvar = ngx_array_push(conf->vars);
            if (nvar == NULL) {
                return NGX_CONF_ERROR;
            }

            *nvar = pvars[i];
        }
    }

    return NGX_CONF_OK;
}


static ngx_int_t
ngx_stream_log_var_set_init(ngx_conf_t *cf)
{
    ngx_stream_handler_pt        *h;
    ngx_stream_core_main_conf_t  *cmcf;
    ngx_array_t                  *arr;

    cmcf = ngx_stream_conf_get_module_main_conf(cf, ngx_stream_core_module);

    arr = &cmcf->phases[NGX_STREAM_LOG_PHASE].handlers;

    h = ngx_array_push(arr);
    if (h == NULL) {
        return NGX_ERROR;
    }

    if (arr->nelts > 1) {
        h = arr->elts;
        ngx_memmove(&h[1], h,
                    (arr->nelts - 1) * sizeof(ngx_stream_handler_pt));
    }

    *h = ngx_stream_log_var_set_handler;

    return NGX_OK;
}
