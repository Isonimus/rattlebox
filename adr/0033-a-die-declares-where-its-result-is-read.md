---
id: '0033'
title: "A die declares where its result is read: the face toward the glass is a property of symmetric dice, not of dice"
type: architecture
status: accepted
date: 2026-11-24
supersedes: []
superseded_by: []
---

## Context

ADR-0009 settled how a result locks: the die comes to rest, it must lie flat, and the numeral is
the one on the face toward the glass. Five dice have shipped on that rule and it has never been
questioned, because all five are centrally symmetric. A D6, a D8, a D12 and a D20 are Platonic
and a D10 is a trapezohedron, and every one of them has, for each face, another face exactly
opposed to it. On such a solid, resting on a face and presenting a face are the same event, so
"the face toward the glass" is a complete rule and nothing distinguishes it from "the face it
rests on".

A tetrahedron is the first die in the product with no opposite face, and it separates the two.
`scripts/tetrahedron-probe.mjs` measures what it does instead:

| What the probe measured, at circumradius 1 | Value |
|---|---|
| Vertices, faces, corners to a face | 4, 4, 3 |
| Inertia per unit mass, every axis | 0.1333333333, off-diagonal 1.39e-18 |
| Against the closed form, an edge squared over 20 | error 2.78e-17 |
| Circumradius of the edge | 0.6123724357, the square root of three eighths |
| Inradius of the circumradius | 0.3333333333 |
| Dihedral angle | 70.5288 degrees |
| Each vertex opposite exactly one face | yes, missing by 0.00e+0 rad |
| With one face down, how squarely the other three face the camera | 0.3333333333, 70.5288 degrees off square |

Two shipped pieces of the product do not merely degrade on that solid, they fail:

- **No result ever locks.** `upFace` returns whichever sloping face wins a tie at 70.5288 degrees
  from the glass, and `updateResult` compares that against `COCKED_TILT_RAD` of 0.5 degrees. A
  D4 lying perfectly flat reads as cocked, is nudged, lands flat, reads as cocked again. The
  failure is silent and permanent: the die never stops being rolled.
- **No numeral is ever drawn.** The renderer shows a face's numeral only when its normal reaches
  `NUMERAL_MIN_FACING`, which is 0.5. The three visible faces of a resting D4 are at a third. The
  die would be drawn as three lit triangles with nothing written on them.

A printed D4 answers both at once, and has for as long as the die has existed: the number is
written at the corners, and the number you read is the one repeated at the apex on all three
visible faces. That number belongs to the face the die is resting on, which you cannot see.

## Decision

**`DieShape` carries where its result is read**, as an enumerated field beside `holdS`. There are
two values. `TopFace` is the rule ADR-0009 set, and is what the five shipped dice declare.
`Apex` is the D4's: the result is the numeral of the face the die rests on, and it is drawn at
the apex standing over that face.

**The apex rule needs no new data, because the solid provides it.** The face read is the one
whose outward normal is most opposed to the glass; the apex is the vertex most aligned with the
glass; and on a tetrahedron the probe measures those to be exactly antipodal, missing by 0.00e+0
radians. So "the resting face's numeral" and "the apex's numeral" are one quantity computed one
way, and nothing has to be stored to connect them.

**Lying flat is measured the same way for both rules**, against the same `COCKED_TILT_RAD`: for
`TopFace`, the angle between the face toward the glass and the glass; for `Apex`, the angle
between the resting face and the tray floor, which is the same measurement turned over. A
propped D4 is nudged by the path ADR-0009 already built.

**The renderer draws the apex numeral on a face that faces the camera and contains the apex
vertex.** That condition is exact and carries no threshold, which is the point:
`NUMERAL_MIN_FACING` governs when a digit still reads on a small face and has nothing to say
about the D4.

**A new die type declares its rule, and a test checks the declaration against the solid.** This
becomes standing invariant #7: a catalog entry declaring `TopFace` must have every face exactly
opposed by another, and one declaring `Apex` must have every vertex exactly opposed by a face. A
declaration is a claim about geometry, so it is checkable, and an unchecked declaration is a
comment.

### What was rejected, and why

**Numerals stored per vertex.** The faithful model of a printed D4, and the one that would let
the renderer write all three digits on each face. Rejected on cost and on legibility. It adds 20
bytes to every `DieShape`, charging six dice for a fact one of them has, which is exactly the
accounting ADR-0030 did when it weighed the shape limits. And it puts nine digits on a resting
D4: the D10's two-digit 10 was already the legibility limit on a 240 by 135 screen with three
dice, and these would be smaller.

**Lowering `NUMERAL_MIN_FACING` so the sloping faces draw.** The one-line change, and wrong twice
over. It would alter what the other five dice draw in order to fix the D4, and it would leave
each sloping face showing its own numeral, none of which is the result: the screen would
contradict the band.

**Reading the D4 from the apex face toward the glass anyway**, by widening `COCKED_TILT_RAD` past
70 degrees for that die. Rejected because the threshold's job is to tell a flat die from a
propped one, and at 70 degrees it can no longer do that for any orientation, so the D4 would lock
results while leaning on a wall.

## Consequences

- A sixth die ships without raising the shape limits: 4 vertices, 4 faces and 3 corners are all
  inside the ceiling ADR-0030 set, so the only static RAM the D4 costs is its own catalog row.
- Standing invariant #7 is added to `CLAUDE.md` in this commit, with the catalog test that
  enforces it.
- The d% still on the roadmap reads faces and declares `TopFace`; it is a numbering and
  presentation problem, not a reading one, and this ADR leaves it untouched.
- The result-reading code now takes a direction rather than assuming the glass. That makes the
  sign of the direction load-bearing, so it is named at both call sites rather than passed as a
  literal.
- ADR-0009's rule is not superseded. It said how a result locks and that the die must lie flat,
  and both still hold for every die. What it assumed without saying was that a die has a face
  toward the glass when it lies flat, which was true of every die that then existed. ADR-0009
  carries a correction marker at that claim pointing here.
