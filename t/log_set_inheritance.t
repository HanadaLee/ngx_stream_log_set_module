#!/usr/bin/perl

# Conditional log_set inheritance.

use warnings;
use strict;
use Test::More;
BEGIN { use FindBin; chdir($FindBin::Bin); }
use Test::Nginx;
use Test::Nginx::Stream qw/stream/;

my @stream = (
    [8081, 'parent'],
    [8082, 'child'],
    [8083, 'first'],
    [8084, 'parent'],
);

my $t = Test::Nginx->new()->has(qw/stream stream_return ngx_expr_module
    ngx_stream_log_set_module/)->plan(2 * scalar @stream);

$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;
events {}
stream {
    %%TEST_GLOBALS_STREAM%%
    log_format values '$server_port|$field|$other';
    expr selected bool true;
    expr missed bool false;
    when selected { log_set $field parent; }
    log_set $field fallback;
    log_set $other keep;

    server {
        listen 127.0.0.1:8081;
        access_log %%TESTDIR%%/stream-values.log values;
        when missed { log_set $field child; }
        return ok;
    }
    server {
        listen 127.0.0.1:8082;
        access_log %%TESTDIR%%/stream-values.log values;
        when selected { log_set $field child; }
        return ok;
    }
    server {
        listen 127.0.0.1:8083;
        access_log %%TESTDIR%%/stream-values.log values;
        log_set $field first;
        when selected { log_set $field unreachable; }
        return ok;
    }
    server {
        listen 127.0.0.1:8084;
        access_log %%TESTDIR%%/stream-values.log values;
        return ok;
    }
}
EOF

$t->run();
for my $case (@stream) {
    is(stream('127.0.0.1:' . port($case->[0]))->read(), 'ok',
        "Stream session $case->[0]");
}
$t->stop();

my $stream_log = $t->read_file('stream-values.log');
for my $case (@stream) {
    my $port = port($case->[0]);
    like($stream_log, qr/^\Q$port|$case->[1]|keep\E$/m,
        "Stream inherited value for $case->[0]");
}
