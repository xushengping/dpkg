#!/usr/bin/perl
#
# dpkg-buildflags
#
# Copyright © 2010 Raphaël Hertzog <hertzog@debian.org>
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.

use strict;
use warnings;

use Dpkg;
use Dpkg::Gettext;
use Dpkg::ErrorHandling;
use Dpkg::BuildFlags;

textdomain("dpkg-dev");

sub version {
    printf _g("Debian %s version %s.\n"), $progname, $version;

    printf _g("
Copyright (C) 2010 Raphael Hertzog <hertzog\@debian.org>.");

    printf _g("
This is free software; see the GNU General Public License version 2 or
later for copying conditions. There is NO warranty.
");
}

sub usage {
    printf _g(
"Usage: %s [<action>]

Actions:
  --get <flag>       output the requested flag to stdout.
  --origin <flag>    output the origin of the flag to stdout:
                     value is one of vendor, system, user, env.
  --list             output a list of the flags supported by the current vendor.
  --export=(sh|make) output commands to be executed in shell or make that export
                     all the compilation flags as environment variables.
  --help             show this help message.
  --version          show the version.
"), $progname;
}

my ($param, $action);

while (@ARGV) {
    $_ = shift(@ARGV);
    if (m/^--(get|origin)$/) {
        usageerr(_g("two commands specified: --%s and --%s"), $1, $action)
            if defined($action);
        $action = $1;
        $param = shift(@ARGV);
	usageerr(_g("%s needs a parameter"), $_) unless defined $param;
    } elsif (m/^--export(?:=(sh|make))?$/) {
        usageerr(_g("two commands specified: --%s and --%s"), "export", $action)
            if defined($action);
        my $type = $1 || "sh";
        $action = "export-$type";
    } elsif (m/^--list$/) {
        usageerr(_g("two commands specified: --%s and --%s"), "list", $action)
            if defined($action);
        $action = "list";
    } elsif (m/^-(h|-help)$/) {
        usage();
        exit 0;
    } elsif (m/^--version$/) {
        version();
        exit 0;
    } else {
	usageerr(_g("unknown option \`%s'"), $_);
    }
}

usageerr(_g("need an action option")) unless defined($action);

my $build_flags = Dpkg::BuildFlags->new();

if ($action eq "list") {
    foreach my $flag ($build_flags->list()) {
	print "$flag\n";
    }
    exit(0);
}

$build_flags->load_config();

if ($action eq "get") {
    if ($build_flags->has($param)) {
	print $build_flags->get($param) . "\n";
	exit(0);
    }
} elsif ($action eq "origin") {
    if ($build_flags->has($param)) {
	print $build_flags->get_origin($param) . "\n";
	exit(0);
    }
} elsif ($action =~ m/^export-(.*)$/) {
    my $export_type = $1;
    foreach my $flag ($build_flags->list()) {
	next unless $flag =~ /^[A-Z]/; # Skip flags starting with lowercase
	my $value = $build_flags->get($flag);
	if ($export_type eq "sh") {
	    $value =~ s/"/\"/g;
	    print "export $flag=\"$value\"\n";
	} elsif ($export_type eq "make") {
	    $value =~ s/\$/\$\$/g;
	    print "export $flag := $value\n";
	}
    }
    exit(0);
}

exit(1);
