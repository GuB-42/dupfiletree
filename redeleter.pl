#!/usr/bin/perl

use strict;

my %todel = ();

my $in_delete = 0;
my $in_group = 0;
open F, $ARGV[0] || die "$!";
while (<F>) {
   chomp;
   if (/^delete size : / && !$in_delete && !$in_group) {
      $in_delete = 1;
   } elsif (/^group size : / && !$in_delete && !$in_group) {
      $in_group = 1;
   } elsif (/^\s*$/ && $in_delete) {
      $in_delete = 0;
   } elsif (/^\s*$/ && $in_group) {
      $in_group = 0;
   } elsif ($in_delete) {
      $todel{$_} = 1;
   } elsif ($in_group) {
      ;
   }
}
close F;
print "OK read\n";

my %vnodes = ();

open F, $ARGV[1] || die "$!";
while (<F>) {
   chomp;

   if (/(\w+)\s+(\d+)\s+(.*)/) {
      my $md5 = $1;
      my $size = $2;
      my $fullpath = $3;

      my $path = "";
      if ($fullpath =~ /%%%%/) {
         for my $p (split /\//, $fullpath) {
            $path .= "$p";
            if (substr($path, -4) eq "%%%%") {
               $vnodes{substr($path, 0, -4)} = "UNKNOWN";
               last;
            }
            $path .= "/";
         }
      }
   }
}
print "OK vnode search\n";

open F, $ARGV[1] || die "$!";
while (<F>) {
   chomp;

   if (/(\w+)\s+(\d+)\s+(.*)/) {
      my $md5 = $1;
      my $size = $2;
      my $fullpath = $3;

      if (defined $vnodes{$fullpath}) {
         $vnodes{$fullpath} = $md5;
      }
   }
}
print "OK vnode get\n";

my %keep_md5_vnode = ();
my %keep_md5_file = ();
my %vnode_md5 = ();
my $prog = 0;
open F, $ARGV[1] || die "$!";
while (<F>) {
   chomp;

   if (/(\w+)\s+(\d+)\s+(.*)/) {
      my $md5 = $1;
      my $size = $2;
      my $fullpath = $3;

      my $is_vnode = 0;
      my $vnode_path = "";
      my $found = 0;
      my $path = "";
      for my $p (split /\//, $fullpath) {
         $path .= "$p";
         if (length($path) > 2) {
            if ($todel{"$path"}) {
               $found = 1;
            }
            if (substr($path, -4) eq "%%%%") {
               $is_vnode = 1;
               $vnode_path = substr($path, 0, -4);
               if ($todel{$vnode_path}) {
                  $found = 1;
                  last;
               }
            }
         }
         $path .= "/";
      }

      if ($found) {
         if ($is_vnode) {
            ++$vnode_md5{$vnodes{$vnode_path}}{$md5};
         }
      } else {
         if ($is_vnode) {
            ++$keep_md5_vnode{$md5};
         } else {
            ++$keep_md5_file{$md5};
         }
      }
   }

   ++$prog;
   print "$prog\n" if (($prog % 100000) == 0)
}
close F;
print "OK xmd5\n";

my $changed = 1;
while ($changed) {
   $changed = 0;
   for my $k (keys %vnode_md5) {
      if (defined $keep_md5_vnode{$k}) {
         for my $v (keys %{$vnode_md5{$k}}) {
            $keep_md5_vnode{$v} = 1;
         }
         delete $vnode_md5{$k};
         $changed = 1;
      }
   }
}
print "OK vnode cascade\n";

open F, $ARGV[1] || die "$!";
while (<F>) {
   chomp;

   if (/(\w+)\s+(\d)+\s+(.*)/) {
      my $md5 = $1;
      my $size = $2;
      my $fullpath = $3;

      unless ($md5 eq "d41d8cd98f00b204e9800998ecf8427e") {
         unless ($fullpath =~ /%%%%/) {
            unless ($keep_md5_vnode{$md5} || $keep_md5_file{$md5}) {
               print "LOST $_\n";
            }
            print "DUPE $keep_md5_file{$md5} $_\n" if ($keep_md5_file{$md5} > 1);
            print "DVNODE $_\n" if ($keep_md5_file{$md5} && $keep_md5_vnode{$md5});
         }
      }
   }
}
close F;

