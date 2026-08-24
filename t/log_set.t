#!/usr/bin/perl

# Tests for ngx_stream_log_set_module with ngx_condition_module.

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

my $t = Test::Nginx->new()->has(qw/stream stream_return ngx_condition_module
	ngx_stream_log_set_module/)->plan(10);

$t->write_file_expand('nginx.conf', <<'EOF');

%%TEST_GLOBALS%%

daemon off;

events {
}

stream {
    %%TEST_GLOBALS_STREAM%%

    log_format values '$server_port|$field|$other';

    log_set $field parent-$status;
    log_set $other parent;

    server {
        listen      127.0.0.1:8080;
        access_log  %%TESTDIR%%/values.log values;
        return      inherit;
    }

    server {
        listen      127.0.0.1:8081;
        access_log  %%TESTDIR%%/values.log values;

        condition special bool true;

        when special {
            log_set $field special-$status;
        }

        log_set $field fallback-$status;
        return first;
    }

    server {
        listen      127.0.0.1:8082;
        access_log  %%TESTDIR%%/values.log values;

        condition special bool false;

        when special {
            log_set $field special-$status;
        }

        log_set $field fallback-$status;
        return second;
    }

    server {
        listen      127.0.0.1:8083;
        access_log  %%TESTDIR%%/values.log values;
        log_set     $field local;
        return      override;
    }

    server {
        listen      127.0.0.1:8084;
        access_log  %%TESTDIR%%/values.log values;
        log_set     $field $protocol:$status:$bytes_sent;
        return      response;
    }
}

EOF

$t->run();

###############################################################################

is(response(8080), 'inherit', 'inherited rule session');
is(response(8081), 'first', 'conditional hit session');
is(response(8082), 'second', 'conditional miss session');
is(response(8083), 'override', 'local override session');
is(response(8084), 'response', 'response variable session');

my $log = $t->read_file('values.log');

like($log, line(8080, 'parent-200', 'parent'),
	'stream rules are inherited');
like($log, line(8081, 'special-200', 'parent'),
	'first matching definition wins');
like($log, line(8082, 'fallback-200', 'parent'),
	'condition miss evaluates fallback');
like($log, line(8083, 'local', 'parent'),
	'local definition replaces parent for the same variable');
like($log, qr/^\Q@{[port(8084)]}\E\|TCP:200:\d+\|parent$/m,
	'log phase can use final session variables');

###############################################################################

sub response {
	return stream('127.0.0.1:' . port(shift))->read();
}


sub line {
	my ($port, $field, $other) = @_;

	return qr/^\Q@{[port($port)]}|$field|$other\E$/m;
}

###############################################################################
