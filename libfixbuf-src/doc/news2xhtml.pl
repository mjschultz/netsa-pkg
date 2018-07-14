#!/usr/bin/perl

## @OPENSOURCE_LICENSE_START@
## libfixbuf 2.0
##
## Copyright 2018 Carnegie Mellon University. All Rights Reserved.
##
## NO WARRANTY. THIS CARNEGIE MELLON UNIVERSITY AND SOFTWARE
## ENGINEERING INSTITUTE MATERIAL IS FURNISHED ON AN "AS-IS"
## BASIS. CARNEGIE MELLON UNIVERSITY MAKES NO WARRANTIES OF ANY KIND,
## EITHER EXPRESSED OR IMPLIED, AS TO ANY MATTER INCLUDING, BUT NOT
## LIMITED TO, WARRANTY OF FITNESS FOR PURPOSE OR MERCHANTABILITY,
## EXCLUSIVITY, OR RESULTS OBTAINED FROM USE OF THE
## MATERIAL. CARNEGIE MELLON UNIVERSITY DOES NOT MAKE ANY WARRANTY OF
## ANY KIND WITH RESPECT TO FREEDOM FROM PATENT, TRADEMARK, OR
## COPYRIGHT INFRINGEMENT.
##
## Released under a GNU-Lesser GPL 3.0-style license, please see
## LICENSE.txt or contact permission@sei.cmu.edu for full terms.
##
## [DISTRIBUTION STATEMENT A] This material has been approved for
## public release and unlimited distribution.  Please see Copyright
## notice for non-US Government use and distribution.
##
## Carnegie Mellon(R) and CERT(R) are registered in the U.S. Patent
## and Trademark Office by Carnegie Mellon University.
##
## DM18-0325
## @OPENSOURCE_LICENSE_END@

use warnings;
use strict;

my $project = $ARGV[0] or die "Expected package name as first argument\n";

# Licence for fixbuf-2.0 and later
my $new_license = 'lgpl3';

# License for older versions of fixbuf
my $old_license = 'lgpl';

# The date when the license changed
my $lgpl3_date = '2018-05-01';

# Date of oldest release that the user is allowed to download.
my $oldest_rel_date = '2015-07-01';  # fixbuf 1.7.0

print <<HEAD;
<?xml version="1.0"?>
<p:project xmlns:p="http://netsa.cert.org/xml/project/1.0"
           xmlns="http://www.w3.org/1999/xhtml"
           xmlns:xi="http://www.w3.org/2001/XInclude">
HEAD

my $ul = 0;
my $li = 0;

# slurp in all of the standard input
my $content;
{
    local $/ = undef;
    $content = <STDIN>;
}


# This regexp is pretty liberal, so as to be able to grok most NEWS formats.
while($content =~ /^Version (\d[^:]*?):?\s+\(?([^\n]+?)\)?\s*\n\s*=+\s*((?:.(?!Version))+)/msg)
{
    my ($ver, $date, $notes) = ($1, $2, $3);

    print <<RELHEAD1;
    <p:release>
        <p:version>$ver</p:version>
        <p:date>$date</p:date>
RELHEAD1

    if ($date ge $oldest_rel_date)
    {
        my $license = (($date ge $lgpl3_date) ? $new_license : $old_license);
        print <<RELHEAD2;
        <p:file href="../releases/$project-$1.tar.gz" license="$license"/>
RELHEAD2
    }

    print <<RELHEAD3;
        <p:notes>
            <ul>
RELHEAD3

    # html escape the notes
    $notes =~ s/&/&amp;/g;
    $notes =~ s/</&lt;/g;
    $notes =~ s/>/&gt;/g;

    # First, see if items are delimited by \n\n
    if ($notes =~m@(.+?)\n\n+?@)
    {
        while ($notes =~m@(.+?)\n\n+?@msg)
        {
            print "\t\t<li>$1</li>\n";
        }
        # The last item will be skipped if there aren't two blank lines
        # at the end, so we look for that and fix it here.
        if ($notes =~ /(.+?)(?:\n(?!\n))$/)
        {
            print "\t\t<li>$1</li>\n";
        }
    }
    # Otherwise, assume items are delimited by \n
    else
    {
        while ($notes =~m@(.*?)\n+@msg)
        {
            print "\t\t<li>$1</li>\n";
        }
    }

    print <<RELTAIL;
             </ul>
        </p:notes>
    </p:release>
RELTAIL
;

}
print <<TAIL;
</p:project>
TAIL
