# TorrentZip

This project tries to enhance the code of the [original
trrntzip](https://sourceforge.net/projects/trrntzip/) implementation
for modern systems.

Torrentzip converts zip archives to a standard format with some
pre-defined values, sorting the files, and using particular
compression settings so that running it on zip archives created by
other tools will always result in the same output. This helps
e.g. with sharing zip archives using
[BitTorrent](https://www.bittorrent.org) (which is where the name
comes from).

# Installation

## Requirements

* A C compiler (e.g. gcc or clang)
* [zlib](http://zlib.net/) (at least version 1.2.2)
* [CMake](https://cmake.org/) (at least version 3.5)

## Building

* mkdir build
* cd build
* cmake ..
* make
* make install

# Status

[![build](https://github.com/0-wiz-0/trrntzip/actions/workflows/build.yml/badge.svg)](https://github.com/0-wiz-0/trrntzip/actions/workflows/build.yml)
