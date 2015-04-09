#!/usr/bin/perl

%todel = ();

open F, $ARGV[0] || die "$!";
while (<F>) {
   chomp;
   my $fullpath = $_;
   $todel{$fullpath} = 1 ;
}
close F;

%keep_md5 = ();

print "ZZ\n";
my $prog = 0;
open F, $ARGV[1] || die "$!";
while (<F>) {
   chomp;

   if (/(\w+)\s+(\w+)\s+(\d)+\s+(.*)/) {
      my $md5 = $1;
      my $crc32 = $2;
      my $size = $3;
      my $fullpath = $4;

      my $found = 0;
      my $path = "";
      for my $p (split /\//, $fullpath) {
         $path .= "$p";
         if (length($path) > 2) {
            if ($todel{"./$path"}) {
               $found = 1;
               last;
            }
         }
         $path .= "/";
      }

      if ($found) {
         ;#print "DEL $fullpath\n";
      } else {
         ++$keep_md5{$md5};
      }
   }

   ++$prog;
   print "$prog\n" if (($prog % 100000) == 0)
}
close F;

print "OK go\n";
open F, $ARGV[1] || die "$!";
while (<F>) {
   chomp;

   if (/(\w+)\s+(\w+)\s+(\d)+\s+(.*)/) {
      my $md5 = $1;
      my $crc32 = $2;
      my $size = $3;
      my $fullpath = $4;

      print "$_\n" unless ($keep_md5{$md5});
      print "DUPE $keep_md5{$md5} $_\n" if ($keep_md5{$md5} > 1);
   }
}
close F;
