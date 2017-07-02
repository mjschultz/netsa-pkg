SiLK, the System for Internet-Level Knowledge, is a collection of
traffic analysis tools developed by the CERT Network Situational
Awareness Team (CERT NetSA) to facilitate security analysis of large
networks. The SiLK tool suite supports the efficient collection,
storage, and analysis of network flow data, enabling network security
analysts to rapidly query large historical traffic data sets. SiLK is
ideally suited for analyzing traffic on the backbone or border of a
large, distributed enterprise or mid-sized ISP.

SiLK comes with NO WARRANTY.  The SiLK software components are
released under the GNU General Public License V2 and Government
Purpose License Rights.  See the files LICENSE.txt and doc/GPL.txt
for details.  Some included library code is covered under LGPL 2.1;
see source files for details.

In general, you can install SiLK by running
    configure ; make ; make install
For details, see doc/INSTALL.txt.


SiLK 4.x is beta software.  The applications have been lightly tested.
Some applications may change in incompatible ways in future releases.

The analysis tools in SiLK 4.x are largely compatible with those in
SiLK 3.x, though SiLK 4.x removes command line switches that were
marked as deprecated in SiLK 3.x.

The configuration of rwflowpack has radically changed from SiLK 3.x.
The flowcap and rwflowappend tools no longer exist.

Replacing a SiLK 3.x installation with SiLK 4.x is not recommended
without substantial testing first.


SiLK 4.x is beta software.  The applications have been lightly tested.
Some applications may change in incompatible ways in future releases.


Manual pages for each tool are installed when SiLK is installed.

The following documents are available in either this directory or in
the doc/ directory:

    RELEASE-NOTES.txt
        -- history of changes to SiLK
    LICENSE.txt
        -- brief description of licenses under which SiLK is released
           and the no warranty disclaimer
    GPL.txt
        -- complete text of the GNU General Public License V2
    INSTALL.txt
        -- a quick-start guide for installing SiLK
    autotools-readme.txt
        -- information for developers on using the AutoTools suite to
           re-create the "configure" script and "Makefile.in" files
