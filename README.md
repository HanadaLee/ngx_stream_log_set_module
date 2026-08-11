# Name

`ngx_stream_log_var_set_module` allows setting the variable to the given value before access log writing.

# Table of Content

- [Name](#name)
- [Table of Content](#table-of-content)
- [Status](#status)
- [Synopsis](#synopsis)
- [Installation](#installation)
- [Conditional syntax](#conditional-syntax)
- [Directives](#directives)
  - [log\_var\_set](#log_var_set)
- [Author](#author)
- [License](#license)

# Status

This Nginx module is currently considered experimental. Issues and PRs are welcome if you encounter any problems.

# Synopsis

```nginx
log_format basic '$remote_addr [$time_local] '
                 '$protocol $status $bytes_sent $bytes_received '
                 '$session_time '
                 '"$log_field1" "$log_field2"';
access_log /spool/logs/nginx-access.log;

server {
    listen 127.0.0.1:12380;
    log_var_set $log_field1 $proxy_protocol_tlv_alpn;
    condition has_ssl_version is_not_empty $proxy_protocol_tlv_ssl_version;
    when has_ssl_version {
        log_var_set $log_field2 $proxy_protocol_tlv_ssl_version;
    }
    proxy_pass 127.0.0.1:12381;
}
```

# Installation

To use theses modules, configure your nginx branch with `--add-module=/path/to/ngx_stream_log_var_set_module`.

To enable named conditions, build `ngx_condition_module` and this module statically in the same nginx configuration.

# Conditional syntax

Conditional syntax is selected at compile time. With `ngx_condition_module`, place `log_var_set` inside a `stream` or `server` `when` block; `if=` and `if!=` are rejected. Without it, `when` is unavailable and legacy `if=`/`if!=` remain supported. A rule whose condition does not match is skipped so the next definition of the same variable can be evaluated.

# Directives

## log_var_set

**Syntax:** *log_var_set $variable value;*

**Default:** *-*

**Context:** *stream, server, when*

Sets the variable to the given value before access log writing. The value may contain variables. These directives are inherited from the previous configuration level only when there is no directive for the same variable defined at the current level.

# Author

Hanada im@hanada.info

# License

This Nginx module is licensed under [BSD 2-Clause License](LICENSE).
