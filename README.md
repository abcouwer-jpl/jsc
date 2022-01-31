# JSC

README for JSC, JPEG-safety-critical

This code is adaptation of the IJG's JPEG, v9d, to better approach 
certain standards for JPL safety-critical software. 
For instance, safety-critical software should never fail. 
Therefore, there are no calls for dynamic memory allocation, which could fail.

The README for JPEG follows.

These modifications were created by Neil Abcouwer at JPL. Neil is by no means 
an expert on JPEG or compression. 

The Apache 2.0 Licence applies to the modifications.

Many features have been removed from both the compressor and decompressors.
As features have been removed from the JPEG decompressor, there are a set of
data inputs that are might meet the JPEG standard, but cannot be processed
by this decompressor. However the use case envisioned by this library is
primarily compression of image buffers on safety-critical systems, then
the compressed output is decompressed by a generic jpeg-reading application 
to be viewed by a human on their non-safety critical system. 
JPEG compression is optimized for human viewing. 

## Using the code

Use `jsc_compress()` (from `include/jsc/jsc_pub.h`) 
to compress an image with one function, given appropriately-sized memory 
buffers for output and working memory.

`include/jsc/jsc_types_pub.h` defines macros for buffer sizing 
at compile time.

jsc expects a configuration dependent `include/jsc/jsc_conf_global_types.h`
and `include/jsc/jsc_conf_private.h` that one must create for your 
specific configuration. `jsc_conf_global_types.h` defines sized types, and 
`jsc_conf_global_types.h` defines macros. 
`test/jsc_test_global_types.h` and `test/jsc_test_private.h` are examples 
that will be copied over to `include/jsc` for unit testing.

### Warning against Decompression

You probably shouldn't be decompressing with your safety-critical application.
Well, you can with the heritage jpeg functions, but if your safety-critical 
application involves reading JPEG-compressed output, and the result affects 
what your system does, you may want to rethink your design.

Two main reasons:

1. JPEG is designed for viewing by humans. Image processing applications 
(stereo, visual odometry, etc) would suffer from the artifacts created by JPEG. 
Use a lossless compressor instead if you're trying to move images 
around for processing.

2. A bad buffer provided to the decompressor may cause issues. 
One test within the included unit tests tries corrupting a compressed buffer 
one byte at a time to evey possible byte value, and confirms that 
decompression returns failure gracefully, but this does not preclude 
issues when multiple bytes are corrupted.

That said, the decompression code still exists, is tested against the 
compression code, and works well. 

## building

This code does not by itself compile into an executable or library. 
This code is intended to be compiled along with the code that uses it.

For testing, you'll need the following dependencies:

`build-essential cmake gcc valgrind lcov`

To run unit tests (including pulling google test framework): 

`./build.bash test`

To run unit tests with coverage reports:

`./build.bash coverage`

Then open ./build/coverage/index.html to look at results.

To save unit test output to the test folder (so it can be committed 
for later delta comparison)

`  ./build.bash save`

To run unit tests with valgrind 
(note, this disables death tests, as they don't play well with valgrind):

`./build.bash valgrind`

To clean (remove the build directory):

`./build.bash clean`

To use with your own framework, you will need to define your own versions of
`jsc_conf_global_types.h` and `jsc_conf_private.h`, 
to do appropriate declarations for your framework, and you may need to make 
build changes so they aren't overwritten.

## List of modifications:

- Added jscstaticmem.c and modified core jpeg files to provide simple static 
    memory allocation from a provided buffer, rather than dynamic memory.
- Added jsc_types_pub.h, jsc_pub.h, and jsc_compress.c for 
    a one-shot compression function, jsc_compress().
- Removed features to reduce testing effort:
  - Removed error handler and jump buffer based error handling
  - Removed DCT and IDCT scaling for non-8x8 MCUs
  - Removed arithmetic encoding/decoding
  - Removed color quantization / color map support
  - Removed use of backing store
  - Removed progressive encoding
  - Removed stdio as data src/dest - only buffers
  - Removed jpeg_write_raw_data() - only scanlines
  - Removed optimized entropy encoding
  - Removed jpeg_write_tables()
- Moved essenitials from jinclude.h, jconfig.h, and jmorecfg.h to 
    jsc_conf_private.h and jsc_conf_public_types.h 
- Applied MISRA rules - no undefs, reduced conditional compilation, 
    no using result of assignment, 
    for loops have brackets
- Undid clever two-pass J_MESSAGE enum/string array.
- Provided estimator macro for required size of working memory
- Moved all #defines out of blocks and to tops of files
- changed all uses of in, unsigned int, long to JINT, JUINT, JLONG 
    for definging in header

Significant modifications have comment with "Abcouwer JSC 2021". 
Code has been fully removed rather than left behind conditional compilation. 
Conditional compilation creates 2^N possible versions of code, 
making testing impossible.

### configuration, types, and macros

This library was written with the intention of being usable for safety-critical 
applications (like spaceflight) and to be flexible enough to be used in 
multiple frameworks. This leads to conflict. For instance, 
safety critical code neccesitates the use of fixed-width types, 
but flexibility means that such types may vary. If not already available, 
users must write a `jsc_conf_global_types.h` to define certain types and a 
`jsc_conf_private.h` to define certain macros. 
`test/jsc_test_global_types.h` and `test/jsc_test_private.h` 
provide more explanation and examples, and are copied when running unit tests.

One example is using NASA Core FSW's `common_types.h` as the basis 
for sized types. Running

`./build.bash cfstest`

copies `configs/cfs/jsc_cfs_global_types.h`, which includes 
`common_types.h` (copied to the test folder for convenience) to 
`jsc_conf_global_types.h`, then builds and runs the unit tests. 
But it is unclear (to Neil) whether Core FSW has assertions (see below) 
so as of yet there is no `jsc_cfs_private.h`.

## assertions

This library was modified with the philosophy that inproper inputs to functions, 
such as an improperly sized image, are programming errors, and that assertions 
be used to check programming errors. See http://spinroot.com/p10/rule5.html 
for more on asserts.

Functions use `JSC_ASSERT` macros. It is the intent that users of the 
jsc library will copy an appropriate `jsc_conf_private.h` to include/jsc, 
such that asserts are defined appropriately for the application.
They might be defined as `ROS_ASSERT`, `BOOST_ASSERT`, 
c `assert`, or send some asycnhronous message to do whatever
the system needs during an anomaly. 

They could also be disabled, but this is is discouraged.

## static analysis

This library has been analyzed using Cobra (http://spinroot.com/cobra/, 
https://github.com/nimble-code/Cobra). 

To use Cobra, follow the Cobra instructions to clone and configure. 
On linux, you may need the "yacc" program from the "bison" package.
You may want to add these lines in your bashrc (or equivalent) 
as discussed in the cobra readme:

`export COBRA=/path/to/your/clone/of/Cobra`

`export PATH=$PATH:$COBRA/bin_linux`

`export C_BASE=$COBRA/rules`

Then run commands of the form:

`cobra -f file -I/path/to/this/repo/include /path/to/this/repo/src/*.c`

Where file can be one of several rules files. This code, compiled with the 
unit test headers, was checked against the basic, misra2012, p10, and jpl rules. 
Running 

`./build.bash cobra`

runs all these checks.

Deviations from the rules:
- The main encoding/decoding loops loop over every pixel of an image or 
the entirety of the compressed bitstream. Thus the overhead of function calls 
becomes significant. Therefore these main loops exceed advised function sizes 
and use macros for the sake of performance.
- P10: Locally-defined macro functions are used to improve performance.
- JPL: Using "ifdef __cplusplus" mangle guard puts code over the preprocessor 
conditional limit of one per header, but that's the price you pay for c++ compatibility.

## contributions

Pull requests or emails about small code fixes, new code features, 
configurations for targeted frameworks, or improvements to the build process 
are welcome. Please ensure any pull requests are fully covered by unit testing, 
follow the established standards, and pass static analysis.

## JPL Development Info  

This software was developed at the Jet Propulsion Laboratory, 
California Institute of Technology, under a contract with the 
National Aeronautics and Space Administration (80NM0018D0004).  

This work was funded by funded by the NASA Space Technology 
Mission Directorate (STMD) and the proposal, 
"CubeRover for Affordable, Modular, and Scalable Planetary Exploration" 
was selected as part of the NASA 2019 Tipping Point solicitation Topic 4: 
Other Capabilities Needed for Exploration. The project is managed by the 
STMD Utilizing Public-Private Partnerships to Advance Tipping Point 
Technologies Program.

This software was developed under JPL Task Plan No 15-106860.

This software is reported via JPL NTR 52154.

This software was approved for open source by JPL Software Release Authority Brian Morrison on 2022-01-06.


# Original JPEG README

The Independent JPEG Group's JPEG software
==========================================

README for release 9d of 12-Jan-2020
====================================

This distribution contains the ninth public release of the Independent JPEG
Group's free JPEG software.  You are welcome to redistribute this software and
to use it for any purpose, subject to the conditions under LEGAL ISSUES, below.

This software is the work of Tom Lane, Guido Vollbeding, Philip Gladstone,
Bill Allombert, Jim Boucher, Lee Crocker, Bob Friesenhahn, Ben Jackson,
John Korejwa, Julian Minguillon, Luis Ortiz, George Phillips, Davide Rossi,
Ge' Weijers, and other members of the Independent JPEG Group.

IJG is not affiliated with the ISO/IEC JTC1/SC29/WG1 standards committee
(previously known as JPEG, together with ITU-T SG16).


DOCUMENTATION ROADMAP
=====================

This file contains the following sections:

OVERVIEW            General description of JPEG and the IJG software.
LEGAL ISSUES        Copyright, lack of warranty, terms of distribution.
REFERENCES          Where to learn more about JPEG.
ARCHIVE LOCATIONS   Where to find newer versions of this software.
ACKNOWLEDGMENTS     Special thanks.
FILE FORMAT WARS    Software *not* to get.
TO DO               Plans for future IJG releases.

Other documentation files in the distribution are:

User documentation:
  install.txt       How to configure and install the IJG software.
  usage.txt         Usage instructions for cjpeg, djpeg, jpegtran,
                    rdjpgcom, and wrjpgcom.
  *.1               Unix-style man pages for programs (same info as usage.txt).
  wizard.txt        Advanced usage instructions for JPEG wizards only.
  change.log        Version-to-version change highlights.
Programmer and internal documentation:
  libjpeg.txt       How to use the JPEG library in your own programs.
  example.c         Sample code for calling the JPEG library.
  structure.txt     Overview of the JPEG library's internal structure.
  filelist.txt      Road map of IJG files.
  coderules.txt     Coding style rules --- please read if you contribute code.

Please read at least the files install.txt and usage.txt.  Some information
can also be found in the JPEG FAQ (Frequently Asked Questions) article.  See
ARCHIVE LOCATIONS below to find out where to obtain the FAQ article.

If you want to understand how the JPEG code works, we suggest reading one or
more of the REFERENCES, then looking at the documentation files (in roughly
the order listed) before diving into the code.


OVERVIEW
========

This package contains C software to implement JPEG image encoding, decoding,
and transcoding.  JPEG (pronounced "jay-peg") is a standardized compression
method for full-color and grayscale images.

This software implements JPEG baseline, extended-sequential, and progressive
compression processes.  Provision is made for supporting all variants of these
processes, although some uncommon parameter settings aren't implemented yet.
We have made no provision for supporting the hierarchical or lossless
processes defined in the standard.

We provide a set of library routines for reading and writing JPEG image files,
plus two sample applications "cjpeg" and "djpeg", which use the library to
perform conversion between JPEG and some other popular image file formats.
The library is intended to be reused in other applications.

In order to support file conversion and viewing software, we have included
considerable functionality beyond the bare JPEG coding/decoding capability;
for example, the color quantization modules are not strictly part of JPEG
decoding, but they are essential for output to colormapped file formats or
colormapped displays.  These extra functions can be compiled out of the
library if not required for a particular application.

We have also included "jpegtran", a utility for lossless transcoding between
different JPEG processes, and "rdjpgcom" and "wrjpgcom", two simple
applications for inserting and extracting textual comments in JFIF files.

The emphasis in designing this software has been on achieving portability and
flexibility, while also making it fast enough to be useful.  In particular,
the software is not intended to be read as a tutorial on JPEG.  (See the
REFERENCES section for introductory material.)  Rather, it is intended to
be reliable, portable, industrial-strength code.  We do not claim to have
achieved that goal in every aspect of the software, but we strive for it.

We welcome the use of this software as a component of commercial products.
No royalty is required, but we do ask for an acknowledgement in product
documentation, as described under LEGAL ISSUES.


LEGAL ISSUES
============

In plain English:

1. We don't promise that this software works.  (But if you find any bugs,
   please let us know!)
2. You can use this software for whatever you want.  You don't have to pay us.
3. You may not pretend that you wrote this software.  If you use it in a
   program, you must acknowledge somewhere in your documentation that
   you've used the IJG code.

In legalese:

The authors make NO WARRANTY or representation, either express or implied,
with respect to this software, its quality, accuracy, merchantability, or
fitness for a particular purpose.  This software is provided "AS IS", and you,
its user, assume the entire risk as to its quality and accuracy.

This software is copyright (C) 1991-2020, Thomas G. Lane, Guido Vollbeding.
All Rights Reserved except as specified below.

Permission is hereby granted to use, copy, modify, and distribute this
software (or portions thereof) for any purpose, without fee, subject to these
conditions:
(1) If any part of the source code for this software is distributed, then this
README file must be included, with this copyright and no-warranty notice
unaltered; and any additions, deletions, or changes to the original files
must be clearly indicated in accompanying documentation.
(2) If only executable code is distributed, then the accompanying
documentation must state that "this software is based in part on the work of
the Independent JPEG Group".
(3) Permission for use of this software is granted only if the user accepts
full responsibility for any undesirable consequences; the authors accept
NO LIABILITY for damages of any kind.

These conditions apply to any software derived from or based on the IJG code,
not just to the unmodified library.  If you use our work, you ought to
acknowledge us.

Permission is NOT granted for the use of any IJG author's name or company name
in advertising or publicity relating to this software or products derived from
it.  This software may be referred to only as "the Independent JPEG Group's
software".

We specifically permit and encourage the use of this software as the basis of
commercial products, provided that all warranty or liability claims are
assumed by the product vendor.


The Unix configuration script "configure" was produced with GNU Autoconf.
It is copyright by the Free Software Foundation but is freely distributable.
The same holds for its supporting scripts (config.guess, config.sub,
ltmain.sh).  Another support script, install-sh, is copyright by X Consortium
but is also freely distributable.


REFERENCES
==========

We recommend reading one or more of these references before trying to
understand the innards of the JPEG software.

The best short technical introduction to the JPEG compression algorithm is
	Wallace, Gregory K.  "The JPEG Still Picture Compression Standard",
	Communications of the ACM, April 1991 (vol. 34 no. 4), pp. 30-44.
(Adjacent articles in that issue discuss MPEG motion picture compression,
applications of JPEG, and related topics.)  If you don't have the CACM issue
handy, a PDF file containing a revised version of Wallace's article is
available at http://www.ijg.org/files/Wallace.JPEG.pdf.  The file (actually
a preprint for an article that appeared in IEEE Trans. Consumer Electronics)
omits the sample images that appeared in CACM, but it includes corrections
and some added material.  Note: the Wallace article is copyright ACM and IEEE,
and it may not be used for commercial purposes.

A somewhat less technical, more leisurely introduction to JPEG can be found in
"The Data Compression Book" by Mark Nelson and Jean-loup Gailly, published by
M&T Books (New York), 2nd ed. 1996, ISBN 1-55851-434-1.  This book provides
good explanations and example C code for a multitude of compression methods
including JPEG.  It is an excellent source if you are comfortable reading C
code but don't know much about data compression in general.  The book's JPEG
sample code is far from industrial-strength, but when you are ready to look
at a full implementation, you've got one here...

The best currently available description of JPEG is the textbook "JPEG Still
Image Data Compression Standard" by William B. Pennebaker and Joan L.
Mitchell, published by Van Nostrand Reinhold, 1993, ISBN 0-442-01272-1.
Price US$59.95, 638 pp.  The book includes the complete text of the ISO JPEG
standards (DIS 10918-1 and draft DIS 10918-2).
Although this is by far the most detailed and comprehensive exposition of
JPEG publicly available, we point out that it is still missing an explanation
of the most essential properties and algorithms of the underlying DCT
technology.
If you think that you know about DCT-based JPEG after reading this book,
then you are in delusion.  The real fundamentals and corresponding potential
of DCT-based JPEG are not publicly known so far, and that is the reason for
all the mistaken developments taking place in the image coding domain.

The original JPEG standard is divided into two parts, Part 1 being the actual
specification, while Part 2 covers compliance testing methods.  Part 1 is
titled "Digital Compression and Coding of Continuous-tone Still Images,
Part 1: Requirements and guidelines" and has document numbers ISO/IEC IS
10918-1, ITU-T T.81.  Part 2 is titled "Digital Compression and Coding of
Continuous-tone Still Images, Part 2: Compliance testing" and has document
numbers ISO/IEC IS 10918-2, ITU-T T.83.
IJG JPEG 8 introduced an implementation of the JPEG SmartScale extension
which is specified in two documents:  A contributed document at ITU and ISO
with title "ITU-T JPEG-Plus Proposal for Extending ITU-T T.81 for Advanced
Image Coding", April 2006, Geneva, Switzerland.  The latest version of this
document is Revision 3.  And a contributed document ISO/IEC JTC1/SC29/WG1 N
5799 with title "Evolution of JPEG", June/July 2011, Berlin, Germany.
IJG JPEG 9 introduces a reversible color transform for improved lossless
compression which is described in a contributed document ISO/IEC JTC1/SC29/
WG1 N 6080 with title "JPEG 9 Lossless Coding", June/July 2012, Paris,
France.

The JPEG standard does not specify all details of an interchangeable file
format.  For the omitted details we follow the "JFIF" conventions, version 2.
JFIF version 1 has been adopted as Recommendation ITU-T T.871 (05/2011) :
Information technology - Digital compression and coding of continuous-tone
still images: JPEG File Interchange Format (JFIF).  It is available as a
free download in PDF file format from http://www.itu.int/rec/T-REC-T.871.
A PDF file of the older JFIF document is available at
http://www.w3.org/Graphics/JPEG/jfif3.pdf.

The TIFF 6.0 file format specification can be obtained by FTP from
ftp://ftp.sgi.com/graphics/tiff/TIFF6.ps.gz.  The JPEG incorporation scheme
found in the TIFF 6.0 spec of 3-June-92 has a number of serious problems.
IJG does not recommend use of the TIFF 6.0 design (TIFF Compression tag 6).
Instead, we recommend the JPEG design proposed by TIFF Technical Note #2
(Compression tag 7).  Copies of this Note can be obtained from
http://www.ijg.org/files/.  It is expected that the next revision
of the TIFF spec will replace the 6.0 JPEG design with the Note's design.
Although IJG's own code does not support TIFF/JPEG, the free libtiff library
uses our library to implement TIFF/JPEG per the Note.


ARCHIVE LOCATIONS
=================

The "official" archive site for this software is www.ijg.org.
The most recent released version can always be found there in
directory "files".  This particular version will be archived as
http://www.ijg.org/files/jpegsrc.v9d.tar.gz, and in Windows-compatible
"zip" archive format as http://www.ijg.org/files/jpegsr9d.zip.

The JPEG FAQ (Frequently Asked Questions) article is a source of some
general information about JPEG.
It is available on the World Wide Web at http://www.faqs.org/faqs/jpeg-faq/
and other news.answers archive sites, including the official news.answers
archive at rtfm.mit.edu: ftp://rtfm.mit.edu/pub/usenet/news.answers/jpeg-faq/.
If you don't have Web or FTP access, send e-mail to mail-server@rtfm.mit.edu
with body
	send usenet/news.answers/jpeg-faq/part1
	send usenet/news.answers/jpeg-faq/part2


ACKNOWLEDGMENTS
===============

Thank to Juergen Bruder for providing me with a copy of the common DCT
algorithm article, only to find out that I had come to the same result
in a more direct and comprehensible way with a more generative approach.

Thank to Istvan Sebestyen and Joan L. Mitchell for inviting me to the
ITU JPEG (Study Group 16) meeting in Geneva, Switzerland.

Thank to Thomas Wiegand and Gary Sullivan for inviting me to the
Joint Video Team (MPEG & ITU) meeting in Geneva, Switzerland.

Thank to Thomas Richter and Daniel Lee for inviting me to the
ISO/IEC JTC1/SC29/WG1 (previously known as JPEG, together with ITU-T SG16)
meeting in Berlin, Germany.

Thank to John Korejwa and Massimo Ballerini for inviting me to
fruitful consultations in Boston, MA and Milan, Italy.

Thank to Hendrik Elstner, Roland Fassauer, Simone Zuck, Guenther
Maier-Gerber, Walter Stoeber, Fred Schmitz, and Norbert Braunagel
for corresponding business development.

Thank to Nico Zschach and Dirk Stelling of the technical support team
at the Digital Images company in Halle for providing me with extra
equipment for configuration tests.

Thank to Richard F. Lyon (then of Foveon Inc.) for fruitful
communication about JPEG configuration in Sigma Photo Pro software.

Thank to Andrew Finkenstadt for hosting the ijg.org site.

Thank to Thomas G. Lane for the original design and development of
this singular software package.

Thank to Lars Goehler, Andreas Heinecke, Sebastian Fuss, Yvonne Roebert,
Andrej Werner, and Ulf-Dietrich Braumann for support and public relations.


FILE FORMAT WARS
================

The ISO/IEC JTC1/SC29/WG1 standards committee (previously known as JPEG,
together with ITU-T SG16) currently promotes different formats containing
the name "JPEG" which is misleading because these formats are incompatible
with original DCT-based JPEG and are based on faulty technologies.
IJG therefore does not and will not support such momentary mistakes
(see REFERENCES).
There exist also distributions under the name "OpenJPEG" promoting such
kind of formats which is misleading because they don't support original
JPEG images.
We have no sympathy for the promotion of inferior formats.  Indeed, one of
the original reasons for developing this free software was to help force
convergence on common, interoperable format standards for JPEG files.
Don't use an incompatible file format!
(In any case, our decoder will remain capable of reading existing JPEG
image files indefinitely.)

The ISO committee pretends to be "responsible for the popular JPEG" in their
public reports which is not true because they don't respond to actual
requirements for the maintenance of the original JPEG specification.
Furthermore, the ISO committee pretends to "ensure interoperability" with
their standards which is not true because their "standards" support only
application-specific and proprietary use cases and contain mathematically
incorrect code.

There are currently different distributions in circulation containing the
name "libjpeg" which is misleading because they don't have the features and
are incompatible with formats supported by actual IJG libjpeg distributions.
One of those fakes is released by members of the ISO committee and just uses
the name of libjpeg for misdirection of people, similar to the abuse of the
name JPEG as described above, while having nothing in common with actual IJG
libjpeg distributions and containing mathematically incorrect code.
The other one claims to be a "derivative" or "fork" of the original libjpeg,
but violates the license conditions as described under LEGAL ISSUES above
and violates basic C programming properties.
We have no sympathy for the release of misleading, incorrect and illegal
distributions derived from obsolete code bases.
Don't use an obsolete code base!

According to the UCC (Uniform Commercial Code) law, IJG has the lawful and
legal right to foreclose on certain standardization bodies and other
institutions or corporations that knowingly perform substantial and
systematic deceptive acts and practices, fraud, theft, and damaging of the
value of the people of this planet without their knowing, willing and
intentional consent.
The titles, ownership, and rights of these institutions and all their assets
are now duly secured and held in trust for the free people of this planet.
People of the planet, on every country, may have a financial interest in
the assets of these former principals, agents, and beneficiaries of the
foreclosed institutions and corporations.
IJG asserts what is: that each man, woman, and child has unalienable value
and rights granted and deposited in them by the Creator and not any one of
the people is subordinate to any artificial principality, corporate fiction
or the special interest of another without their appropriate knowing,
willing and intentional consent made by contract or accommodation agreement.
IJG expresses that which already was.
The people have already determined and demanded that public administration
entities, national governments, and their supporting judicial systems must
be fully transparent, accountable, and liable.
IJG has secured the value for all concerned free people of the planet.

A partial list of foreclosed institutions and corporations ("Hall of Shame")
is currently prepared and will be published later.


TO DO
=====

Version 9 is the second release of a new generation JPEG standard
to overcome the limitations of the original JPEG specification,
and is the first true source reference JPEG codec.
More features are being prepared for coming releases...

Please send bug reports, offers of help, etc. to jpeg-info@jpegclub.org.
