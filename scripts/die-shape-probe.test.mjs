// Tests for the pure half of scripts/die-shape-probe.mjs (slice 0027). The probe's output
// becomes constants in the firmware, so these check the geometry it builds and the arithmetic
// it measures with, not just that it returns numbers.
import { test } from 'node:test';
import assert from 'node:assert/strict';

import {
  atCircumradius,
  bisect,
  CHECKS,
  dihedralAngles,
  edgeLengths,
  faceNormal,
  GEOMETRY_TOLERANCE,
  inertia,
  inradii,
  isotropicParameter,
  measure,
  oppositeFaces,
  selfCheck,
  trapezohedron,
} from './die-shape-probe.mjs';

const ISOTROPIC_T = 0.767463;

const dot = (a, b) => a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
const sub = (a, b) => [a[0] - b[0], a[1] - b[1], a[2] - b[2]];
const norm = (a) => Math.sqrt(dot(a, a));

test('the self-check reproduces inertia known in closed form, which is what makes the rest believable', () => {
  for (const { name, expected, got, error } of selfCheck()) {
    assert.ok(error < GEOMETRY_TOLERANCE, `${name}: expected ${expected}, got ${got}`);
  }
});

test('the self-check would catch arithmetic that is wrong by a scale factor', () => {
  // Inertia goes as length squared, so a solid built twice as large must measure four times
  // the inertia. A measurement that ignored the scale would pass the fixtures and fail here.
  const cube = CHECKS[0].solid();
  const doubled = { vertices: cube.vertices.map((v) => v.map((c) => c * 2)), faces: cube.faces };
  assert.ok(Math.abs(inertia(doubled).principal[0] - 4 * inertia(cube).principal[0]) < GEOMETRY_TOLERANCE);
});

test('a cube measures the volume and the three equal moments it must have', () => {
  const { volume, principal, offDiagonal } = inertia(CHECKS[0].solid());
  assert.ok(Math.abs(volume - 8) < GEOMETRY_TOLERANCE, `volume ${volume}`);
  for (const moment of principal) assert.ok(Math.abs(moment - 4 / 6) < GEOMETRY_TOLERANCE);
  assert.ok(offDiagonal < GEOMETRY_TOLERANCE);
});

test('the trapezohedron is a closed solid of 12 vertices and 10 four-cornered faces', () => {
  const solid = trapezohedron(ISOTROPIC_T);
  assert.equal(solid.vertices.length, 12);
  assert.equal(solid.faces.length, 10);
  for (const face of solid.faces) assert.equal(face.length, 4);
  // Euler's formula for a solid with no holes: V - E + F = 2.
  const edges = new Set();
  for (const face of solid.faces) {
    for (let i = 0; i < face.length; i++) {
      const [a, b] = [face[i], face[(i + 1) % face.length]];
      edges.add(a < b ? `${a},${b}` : `${b},${a}`);
    }
  }
  assert.equal(12 - edges.size + 10, 2);
});

test('every face is wound outward, so the tetrahedra add up instead of cancelling', () => {
  const solid = trapezohedron(ISOTROPIC_T);
  solid.faces.forEach((face, i) => {
    assert.ok(dot(faceNormal(solid, i), solid.vertices[face[0]]) > 0, `face ${i} is wound inward`);
  });
  assert.ok(inertia(solid).volume > 0);
});

test('every face is planar, which is the whole reason the solid is built as a dual', () => {
  const solid = trapezohedron(ISOTROPIC_T);
  solid.faces.forEach((face, i) => {
    const normal = faceNormal(solid, i);
    const plane = dot(normal, solid.vertices[face[0]]);
    for (const corner of face) {
      assert.ok(Math.abs(dot(normal, solid.vertices[corner]) - plane) < GEOMETRY_TOLERANCE, `face ${i} is not flat`);
    }
  });
});

test('every face is a kite: two pairs of equal adjacent edges, and only two lengths in the solid', () => {
  const solid = atCircumradius(trapezohedron(ISOTROPIC_T), 1);
  const lengths = edgeLengths(solid);
  assert.equal(lengths.length, 2);
  for (const face of solid.faces) {
    const sides = face.map((corner, i) => norm(sub(solid.vertices[corner], solid.vertices[face[(i + 1) % face.length]])));
    // A kite's two equal pairs are adjacent. Which pair the corner list starts on depends on
    // where the winding began, so either split is a kite and a split into opposite pairs is a
    // parallelogram, which is what this rejects.
    const equal = (i, j) => Math.abs(sides[i] - sides[j]) < GEOMETRY_TOLERANCE;
    assert.ok((equal(0, 1) && equal(2, 3)) || (equal(1, 2) && equal(3, 0)), `not a kite: ${sides}`);
    assert.ok(!equal(0, 2) || !equal(1, 3) || equal(0, 1), `a parallelogram, not a kite: ${sides}`);
  }
});

test('every face is the same distance from the centre, which is what makes the die fair', () => {
  const distances = inradii(atCircumradius(trapezohedron(ISOTROPIC_T), 1));
  assert.ok(Math.max(...distances) - Math.min(...distances) < GEOMETRY_TOLERANCE);
});

test('each face is exactly opposed by one other, so a numeral has a well defined opposite', () => {
  const { worst, involution } = oppositeFaces(atCircumradius(trapezohedron(ISOTROPIC_T), 1));
  assert.ok(worst < GEOMETRY_TOLERANCE, `worst ${worst}`);
  assert.equal(involution, true);
});

test('a solid whose faces are not opposed in pairs is reported, not passed', () => {
  // An icosahedron is centrally symmetric; a tetrahedron is not, and must fail the check that
  // the D10 relies on. Four corners of a cube, every other one, make a regular tetrahedron.
  const cube = CHECKS[0].solid();
  const corners = [[1, 1, 1], [1, -1, -1], [-1, 1, -1], [-1, -1, 1]];
  const vertices = corners;
  const faces = [[0, 2, 1], [0, 1, 3], [0, 3, 2], [1, 2, 3]];
  const tetrahedron = { vertices, faces };
  // Wind every face outward first, so the failure is about symmetry and not about winding.
  tetrahedron.faces = faces.map((face, i) => (dot(faceNormal(tetrahedron, i), vertices[face[0]]) > 0 ? face : [...face].reverse()));
  assert.ok(oppositeFaces(tetrahedron).worst > 0.1, 'a tetrahedron must not look centrally symmetric');
  assert.ok(cube.faces.length === 6);
});

test('the isotropic proportion has three equal moments and no off-diagonal term', () => {
  const t = isotropicParameter();
  assert.ok(Math.abs(t - ISOTROPIC_T) < 1e-6, `t ${t}`);
  const m = measure(t);
  const [ixx, iyy, izz] = m.principal;
  assert.ok(Math.abs(ixx - iyy) < GEOMETRY_TOLERANCE);
  assert.ok(Math.abs(ixx - izz) < GEOMETRY_TOLERANCE);
  assert.ok(m.offDiagonal < GEOMETRY_TOLERANCE, `off-diagonal ${m.offDiagonal}`);
  assert.ok(m.spread < 1e-9, `spread ${m.spread}`);
});

test('the isotropic proportion is the one the firmware constants are taken from', () => {
  const m = measure(isotropicParameter());
  // Quoted in slices/0027-d10.md and used in src/physics/physics_config.h.
  assert.ok(Math.abs(m.principal[0] - 0.202229) < 1e-6, `I/m ${m.principal[0]}`);
  assert.ok(Math.abs(m.edges[0] - 0.565306) < 1e-6, `short edge ${m.edges[0]}`);
  assert.ok(Math.abs(m.edges[1] - 1.232854) < 1e-6, `long edge ${m.edges[1]}`);
  assert.ok(Math.abs(m.heightOverWidth - 1.178570) < 1e-6, `height/width ${m.heightOverWidth}`);
  assert.ok(Math.abs(m.apexOverRing - 1.169551) < 1e-6, `apex/ring ${m.apexOverRing}`);
});

test('the inertia is anisotropic away from that proportion, so the choice is not arbitrary', () => {
  assert.ok(measure(0.9).spread > 0.09, 'every vertex on one sphere should spread about 10%');
  assert.ok(measure(0.6).spread > 0.17);
});

test('the two dihedral angles are the ones the rest thresholds have to be re-measured against', () => {
  const [sharp, blunt] = measure(isotropicParameter()).dihedral;
  assert.ok(Math.abs(sharp - 97.9589) < 1e-3, `sharp ${sharp}`);
  assert.ok(Math.abs(blunt - 124.4124) < 1e-3, `blunt ${blunt}`);
});

test('a cube has one dihedral angle and it is a right angle', () => {
  assert.deepEqual(
    dihedralAngles(CHECKS[0].solid()).map((a) => Math.round(a * 1e6) / 1e6),
    [90],
  );
});

test('scaling to a circumradius puts the furthest vertex exactly there', () => {
  const solid = atCircumradius(trapezohedron(ISOTROPIC_T), 10);
  assert.ok(Math.abs(Math.max(...solid.vertices.map(norm)) - 10) < GEOMETRY_TOLERANCE);
});

test('bisect refuses a bracket with no sign change rather than returning an endpoint', () => {
  assert.throws(() => bisect((t) => t + 1, 0, 1), /no sign change/);
});
