#!/usr/bin/perl

use strict;

my %todel = ();
my %delgroup = ();
my %verboten = ();

while (<>) {
	chomp;
	if (/^group size : /) {
		%delgroup = ();
	} elsif (/^\s*$/) {
		my %newgroup = ();
		my %fgroup = ();

		for my $fullpath (keys %delgroup) {
			my $found = 0;
			my $path = "";
			for my $p (split /\//, $fullpath) {
				$path .= "$p";
				last if ($path eq $fullpath);
				if ($todel{$path} || $delgroup{$path}) {
					$fgroup{$path}{$fullpath} = $delgroup{$fullpath};
					$found = 1;
				}
				$path .= "/";
			}
			$newgroup{$fullpath} = $delgroup{$fullpath} unless ($found);
		}

		for my $fg (keys %fgroup) {
			for (keys %{$fgroup{$fg}}) {
				delete $fgroup{$fg}{$_} if defined $fgroup{$_};
			}
		}

		my $keeper = undef;
		foreach my $fullpath (keys %newgroup) {
			if ($newgroup{$fullpath} eq 'M' && $verboten{$fullpath}) {
				$keeper = $fullpath;
				last;
			}
		}
		unless ($keeper) {
			foreach my $fullpath (keys %newgroup) {
				if ($newgroup{$fullpath} eq 'M') {
					my $path = "";
					for my $p (split /\//, $fullpath) {
						$path .= "$p";
						last if ($path eq $fullpath);
						$verboten{$path} = 1;
						$path .= "/";
					}
					$keeper = $fullpath;
					last;
				}
			}
		}
		if ($keeper) {
			foreach my $fullpath (keys %newgroup) {
				if ($fullpath ne $keeper && !$verboten{$fullpath}) {
					print "$fullpath\n";
					$todel{$fullpath} = 1;
				} elsif ($fullpath eq $keeper && $fgroup{$keeper}) {
					my $subkeeper = undef;
					my $first = 1;
					foreach my $gp (keys %{$fgroup{$keeper}}) {
						if ($fgroup{$keeper}{$gp} eq 'M' && $verboten{$gp}) {
							$subkeeper = $gp;
							last;
						}
					}
					unless ($subkeeper) {
						foreach my $gp (keys %{$fgroup{$keeper}}) {
							if ($fgroup{$keeper}{$gp} eq 'M') {
								my $path = "";
								for my $p (split /\//, $gp) {
									$path .= "$p";
									last if ($path eq $gp);
									$verboten{$path} = 1;
									$path .= "/";
								}
								$subkeeper = $gp;
								last;
							}
						}
					}
					if ($subkeeper) {
						foreach my $gp (keys %{$fgroup{$keeper}}) {
							if ($gp ne $subkeeper && !$verboten{$gp}) {
								print "$gp\n";
								$todel{$gp} = 1;
							}
						}
					}
				}
			}
		}
	} elsif (/^ M \d+ (.*)/) {
		$delgroup{$1} = 'M';
	} elsif (/^ S \d+ (.*)/) {
		$delgroup{$1} = 'S';
	}
}