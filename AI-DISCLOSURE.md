# AI disclosure

This fork of libbtbb was developed largely with the help of AI coding agents,
directed by the maintainer. This note explains how, so that anyone using,
packaging, or reviewing the fork can judge it with that in mind.

## What this means

- The maintainer directed the work, reviewed it, and takes responsibility for
  the result. The agents were a tool; the decisions, and the accountability,
  are human.
- Most of the library, test, and documentation changes in this fork since the
  upstream `2020-12-R1` release were written or revised by large-language-model
  coding agents working to a written ruleset, with a human reviewing what
  landed.
- Changes that were not trivial were reviewed by a second, independent agent on
  a different model family before being considered for merge.

## What the agents were and were not allowed to do

Agents followed a written set of rules. In short:

- An agent could merge a change on its own only when the change was low-stakes
  and its correct behavior was already settled — by the documentation, the
  Bluetooth specification, or a decision the maintainer had already made — and
  only after its tests passed.
- Everything else was prepared and left for the maintainer to decide and merge:
  changes to the public API, behavior changes, and anything an agent was unsure
  about.
- Design questions were escalated to the maintainer rather than guessed.

## How changes were verified

- Every change was built and unit-tested. The decoders and capture-file writers
  are tested against the Bluetooth specification's sample data and with
  fuzzing, and the tables are checked against their generators.
- Fixes whose effect can only be seen on the air are verified through the
  Ubertooth fork that pins this library, on real hardware, and recorded there.
- Where a fix could not yet be exercised on hardware, it rests on the unit
  tests, and that is stated rather than presented as a measured result.

## Honesty about scope

This was also an exercise in learning to orchestrate AI agents, and the fork's
footprint is large. Not every change was strictly necessary. The aim is that
what remains is of real value — correctness fixes to the Classic and Low Energy
decoders, to clock and hop recovery, and to the capture-file writers — built on
the work of the original authors.

## Credit

libbtbb is the Bluetooth baseband library created by Great Scott Gadgets, with
Michael Ossmann, and developed by that team and many contributors over many
years. This fork builds on theirs. Where it fixes or extends something, it owes
the original a great deal.

## Licensing

Contributions here, however they were produced, are offered under the same
license as the rest of this repository (see `LICENSE`). The maintainer vouches
for them.
