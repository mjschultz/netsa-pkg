#!/usr/bin/perl

use strict;
use warnings;
use Getopt::Long qw(:config gnu_compat permute no_getopt_compat no_bundling);
use Pod::Usage;

# get basename of this script
my $APPNAME = $0;
$APPNAME =~ s,.*/,,;

# the lua compiler
my $luac;

# any additional arguments to pass to $luac
my @luac_args;

# the input file, file.lua
my $input_file;

# where to write the output
my $output_file;


my $output;

parse_options();

# remove the output file if an error occurs
my $remove_output = 1;

END {
    if ($remove_output && defined($output_file)) {
        unlink $output_file;
    }
}


# open the output file
if (!defined $output_file) {
    $output = *STDOUT;
}
elsif (!open $output, ">", $output_file) {
    die "$APPNAME: Cannot open output file '$output_file': $!\n";
}


# invoke the Lua compiler, luac
my @cmd = ($luac, @luac_args, '-o', '-', $input_file);
open BIN, "-|", @cmd
    or die "$APPNAME: Could not run '@cmd': $!\n";
binmode BIN;


# read the result of luac and convert it to ascii
my $buf;
my $first = 1;

while (read(BIN, $buf, 10)) {
    my @out = map {sprintf("0x%02X", $_)} unpack("C*", $buf);
    if ($first) {
        $first = 0;
        print $output join ", ", @out;
    }
    else {
        print $output ",\n", join ", ", @out;
    }
}
if (!$first) {
    print $output "\n";
}


# wait() for the luac process and check the return value
if (!close BIN) {
    if ($!) {
        die "$APPNAME: Error closing pipe to '@cmd': $!\n";
    }
    if ($? & 0x7F) {
        die("$APPNAME: Process '@cmd' exited on signal ", ($? & 0x7F),
            (($? & 0x80) ? ", core dumped" : ""), "\n");
    }
    die "$APPNAME: Process '@cmd' exited with status ", ($? >> 8), "\n";
}


# close the output file
if ($output_file) {
    close $output
        or die "$APPNAME: Could not close output file '$output_file': $!\n";
}


# success
$remove_output = 0;
exit 0;


sub parse_options
{
    # local vars
    my ($opt_luac, $opt_help, $opt_man);

    # process options.  see "man Getopt::Long"
    GetOptions(
        'luac=s'    => \$opt_luac,
        'strip'     => sub { push @luac_args, '-s' },

        'help'      => \$opt_help,
        'man'       => \$opt_man,
        ) or pod2usage( -exitval => 1 );

    # help?
    if ($opt_help) {
        pod2usage(-exitval => 0);
    }
    if ($opt_man) {
        pod2usage(-exitval => 0, -verbose => 2);
    }

    if ($opt_luac) {
        $luac = $opt_luac;
    }
    else {
        $luac = 'luac';
    }

    if (@ARGV > 2) {
        pod2usage(-exitval => 1);
    }
    if (0 == @ARGV) {
        $input_file = "-";
    }
    else {
        $input_file = shift @ARGV;
        if (@ARGV && $ARGV[0] ne "-") {
            $output_file = shift @ARGV;
        }
    }
}
# parse_options


__END__

=head1 NAME

B<lua2hexdump> - Compile a Lua source file and write a hexdump of the result

=head1 SYNOPSIS

 lua2hexdump [--luac=luac] [--strip] [file.lua [file.out]]

=head1 DESCRIPTION

B<lua2hexdump> compiles the lua source file named by the first
argument using the B<luac(1)> compiler, and writes a hexdump of the
result suitable for incorporating into a C array of uint8_t to the
path specified by the second argument.

If the second argument is not specified, results are written to the
standard output.  if the first argument is not specified, input is
read from the standard input.  A value of C<-> may be used to specify
the standard input or standard output explicitly.

=head1 OPTIONS

Option names may be abbreviated if the abbreviation is unique or is an
exact match for an option.  A parameter to an option may be specified
as B<--arg>=I<param> or B<--arg> I<param>.

=over 4

=item B<--luac>=I<LUAC>

The path to the B<luac> binary.  If this is not provided,
B<lua2hexdump> assumes the B<luac> file exists on your PATH.

=item B<--strip>

Whether to strip debugging information from the file.

=back

=head1 SEE ALSO

B<luac(1)>

=cut

# Local Variables:
# mode:perl
# indent-tabs-mode:nil
# End:
