#!/usr/bin/perl

# Tests for legacy log_set filters without ngx_condition_module.

###############################################################################

use warnings;
use strict;

use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }

use lib 'lib';
use Test::Nginx;
use Test::Nginx::Stream qw/ stream /;

###############################################################################

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()
	->has(qw/stream stream_return ngx_stream_log_set_module/)
	->plan(4);

$t->write_file_expand('nginx.conf', <<'EOF');

%%TEST_GLOBALS%%

daemon off;

events {
}

stream {
    %%TEST_GLOBALS_STREAM%%

    log_format values '$server_port|$field';

    map $remote_addr $empty {
        default "";
    }

    server {
        listen      127.0.0.1:8080;
        access_log  %%TESTDIR%%/values.log values;

        log_set $field positive if=$remote_addr;
        log_set $field fallback;

        return positive;
    }

    server {
        listen      127.0.0.1:8081;
        access_log  %%TESTDIR%%/values.log values;

        log_set $field skipped if=$empty;
        log_set $field fallback;

        return fallback;
    }

    server {
        listen      127.0.0.1:8082;
        access_log  %%TESTDIR%%/values.log values;

        log_set $field negative if!=$empty;
        log_set $field fallback;

        return negative;
    }

    server {
        listen      127.0.0.1:8083;
        access_log  %%TESTDIR%%/values.log values;

        log_set $field skipped if!=$remote_addr;
        log_set $field fallback;

        return inverse-fallback;
    }
}

EOF

$t->run();

###############################################################################

response($_) for 8080 .. 8083;

my $log = $t->read_file('values.log');

like($log, line(8080, 'positive'), 'if= selects a truthy rule');
like($log, line(8081, 'fallback'), 'if= miss evaluates fallback');
like($log, line(8082, 'negative'), 'if!= selects an empty rule');
like($log, line(8083, 'fallback'), 'if!= miss evaluates fallback');

###############################################################################

sub response {
	return stream('127.0.0.1:' . port(shift))->read();
}


sub line {
	my ($port, $field) = @_;

	return qr/^\Q@{[port($port)]}|$field\E$/m;
}

###############################################################################
