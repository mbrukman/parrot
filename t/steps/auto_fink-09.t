#! perl
# Copyright (C) 2007, The Perl Foundation.
# $Id$
# auto_fink-09.t

use strict;
use warnings;
use Test::More;
plan( skip_all => 'Fink is Darwin only' ) unless $^O =~ /darwin/;
plan( tests => 12 );
use Carp;
use File::Temp;
use lib qw( lib t/configure/testlib );
use_ok('config::init::defaults');
use_ok('config::auto::fink');
use Parrot::Configure;
use Parrot::Configure::Options qw( process_options );
use Parrot::Configure::Test qw( test_step_thru_runstep);
use IO::CaptureOutput qw| capture |;

my $args = process_options( {
    argv            => [ q{--verbose} ],
    mode            => q{configure},
} );

my $conf = Parrot::Configure->new();

test_step_thru_runstep($conf, q{init::defaults}, $args);

my ($task, $step_name, $step, $ret);
my $pkg = q{auto::fink};

$conf->add_steps($pkg);
$conf->options->set(%{$args});
$task = $conf->steps->[-1];
$step_name   = $task->step;

$step = $step_name->new();
ok(defined $step, "$step_name constructor returned defined value");
isa_ok($step, $step_name);

{
    # mock Fink config file with non-existent Basepath
    my $tfile = File::Temp->new();
    open my $fh, ">", $tfile
        or croak "Unable to open temporary file for writing";
    print $fh "Basepath: /my/phony/directory\n";
    close $fh or croak "Unable to close temporary file after writing";
    $step->{fink_conf} = $tfile;

    my ($rv, $stdout);
    capture(
        sub { $rv = $step->runstep($conf); },
        \$stdout,
    );
    ok(! defined $rv,
        "runstep() returned undef due to unlocateable Fink directories");
    is($step->result(), q{failed},
        "Got expected result for unlocateable Fink directories");
    like($stdout,
        qr/Could not locate Fink directories/,
        "Got expected verbose output for unlocateable Fink directories");
}

pass("Completed all tests in $0");

################### DOCUMENTATION ###################

=head1 NAME

auto_fink-09.t - test config::auto::fink

=head1 SYNOPSIS

    % prove t/steps/auto_fink-09.t

=head1 DESCRIPTION

The files in this directory test functionality used by F<Configure.pl>.

The tests in this file test config::auto::fink in the case where the one or
more of the Fink directories cannot be located.

=head1 AUTHOR

James E Keenan

=head1 SEE ALSO

config::auto::fink, F<Configure.pl>.

=cut

# Local Variables:
#   mode: cperl
#   cperl-indent-level: 4
#   fill-column: 100
# End:
# vim: expandtab shiftwidth=4:
