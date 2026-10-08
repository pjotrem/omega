# Omega

A from-scratch web browser project, written in C.

## Status

- [x] HTML parser (subset of HTML 4.01 / basic HTML5)
- [ ] Network (HTTP)
- [ ] CSS parser
- [ ] Layout engine
- [ ] Renderer
- [ ] JavaScript

## Current state

Right now Omega can parse HTML into a DOM tree and dump it to stdout.
It handles basic tags, attributes, entities, comments, and void elements.

## Build

    gcc -O2 -Wall -Wextra -std=c11 -o htmlparse htmlparse.c

## Usage

    ./htmlparse test.html
    ./htmlparse            # uruchamia wbudowane demo

## Structure

- `htmlparse.c` – parser HTML
