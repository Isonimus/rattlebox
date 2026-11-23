#!/usr/bin/env node
// Die shape probe (slice 0027): which pentagonal trapezohedron the D10 should be.
//
// A trapezohedron is a one-parameter family, and DieShape carries inertia as a single scalar
// because every die so far has been a Platonic solid, whose inertia is the same about every
// axis. This sweeps the family and finds the proportion where that is true of a trapezohedron
// too, so the scalar stays exact and src/physics/world.cpp needs no tensor.
//
// It checks its own arithmetic against two solids whose inertia is known in closed form before
// reporting anything, because every number it prints ends up as a constant in the firmware.
//
// Usage: node scripts/die-shape-probe.mjs
//
// A probe answers its question once and its numbers land in the slice; it is not a verify
// script, so it is not wired into package.json (CLAUDE.md section 3). Everything above main()
// is pure and tested by scripts/die-shape-probe.test.mjs.

import { pathToFileURL } from 'node:url';

const TURN = (2 * Math.PI) / 5;
// Where a dot product that should be zero, or a length that should match, is called equal.
// Chosen well above double rounding (around 1e-16 here) and far below any real asymmetry.
export const GEOMETRY_TOLERANCE = 1e-12;

const sub = (a, b) => [a[0] - b[0], a[1] - b[1], a[2] - b[2]];
const dot = (a, b) => a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
const scale = (a, k) => [a[0] * k, a[1] * k, a[2] * k];
const norm = (a) => Math.sqrt(dot(a, a));
const cross = (a, b) => [
  a[1] * b[2] - a[2] * b[1],
  a[2] * b[0] - a[0] * b[2],
  a[0] * b[1] - a[1] * b[0],
];

/**
 * @typedef {{vertices: number[][], faces: number[][]}} Solid
 *   Vertices in body coordinates, and faces as corner indices wound counter-clockwise seen
 *   from outside, which is what DieShape stores.
 */

/** @returns {number[]} the outward unit normal of the face at index @p faceIndex. */
export function faceNormal(solid, faceIndex) {
  const [a, b, c] = solid.faces[faceIndex].slice(0, 3).map((i) => solid.vertices[i]);
  const n = cross(sub(b, a), sub(c, a));
  return scale(n, 1 / norm(n));
}

/**
 * @brief The pentagonal trapezohedron as the dual of a pentagonal antiprism of half-height t.
 *
 * Dualising is what makes the kites planar. Solving the four corners' coplanarity directly has
 * roots that are not the convex solid, and picking the wrong one gives a shape that looks
 * right in a table of numbers and is a spike in three dimensions.
 *
 * @returns {Solid} 12 vertices and 10 kite faces.
 */
export function trapezohedron(t) {
  const prism = [];
  for (let k = 0; k < 5; k++) prism.push([Math.cos(k * TURN), Math.sin(k * TURN), t]);
  for (let k = 0; k < 5; k++) {
    prism.push([Math.cos(k * TURN + TURN / 2), Math.sin(k * TURN + TURN / 2), -t]);
  }

  // Each of the antiprism's faces becomes one vertex of the dual.
  const prismFaces = [[0, 1, 2, 3, 4], [5, 6, 7, 8, 9]];
  for (let k = 0; k < 5; k++) {
    prismFaces.push([k, (k + 1) % 5, 5 + k]);
    prismFaces.push([5 + k, 5 + ((k + 1) % 5), (k + 1) % 5]);
  }
  const vertices = prismFaces.map((face) => {
    const [a, b, c] = face.slice(0, 3).map((i) => prism[i]);
    let n = cross(sub(b, a), sub(c, a));
    let d = dot(n, a);
    if (d < 0) [n, d] = [scale(n, -1), -d];
    return scale(n, 1 / d);
  });

  // Each of the antiprism's vertices becomes one kite: the duals of the faces that met there.
  const faces = Array.from({ length: 10 }, (unused, v) => {
    const around = prismFaces.flatMap((face, i) => (face.includes(v) ? [i] : []));
    const centre = [0, 1, 2].map((ax) => around.reduce((s, i) => s + vertices[i][ax], 0) / around.length);
    // Sort the corners by angle about the kite's own centre, so it comes out a simple polygon
    // rather than a bow tie, then wind it outward.
    const axis = scale(centre, 1 / norm(centre));
    const first = sub(vertices[around[0]], centre);
    const ref = scale(first, 1 / norm(first));
    const side = cross(axis, ref);
    const angle = (k) => Math.atan2(dot(sub(vertices[k], centre), side), dot(sub(vertices[k], centre), ref));
    around.sort((i, j) => angle(i) - angle(j));
    const [a, b, c] = around.slice(0, 3).map((i) => vertices[i]);
    if (dot(cross(sub(b, a), sub(c, a)), centre) < 0) around.reverse();
    return around;
  });
  return { vertices, faces };
}

/** @returns {Solid} @p solid scaled so its furthest vertex sits at @p radius. */
export function atCircumradius(solid, radius) {
  const furthest = Math.max(...solid.vertices.map(norm));
  return { vertices: solid.vertices.map((v) => scale(v, radius / furthest)), faces: solid.faces };
}

/**
 * @brief Volume and the inertia tensor per unit mass of a uniform solid.
 *
 * Exact, not sampled: the solid is cut into tetrahedra from the origin and each one's second
 * moments are summed in closed form.
 *
 * @returns {{volume: number, principal: number[], offDiagonal: number}} the moments about the
 *   three coordinate axes, and the largest off-diagonal term, which is zero when those axes are
 *   the principal ones.
 */
export function inertia(solid) {
  let volume = 0;
  const covariance = [[0, 0, 0], [0, 0, 0], [0, 0, 0]];
  for (const face of solid.faces) {
    for (let i = 1; i < face.length - 1; i++) {
      const [p, q, s] = [solid.vertices[face[0]], solid.vertices[face[i]], solid.vertices[face[i + 1]]];
      const determinant = dot(p, cross(q, s));
      volume += determinant / 6;
      for (let a = 0; a < 3; a++) {
        for (let b = 0; b < 3; b++) {
          const squares = p[a] * p[b] + q[a] * q[b] + s[a] * s[b];
          const pairs = p[a] * q[b] + q[a] * p[b] + p[a] * s[b] + s[a] * p[b] + q[a] * s[b] + s[a] * q[b];
          covariance[a][b] += (determinant * (2 * squares + pairs)) / 120;
        }
      }
    }
  }
  const trace = covariance[0][0] + covariance[1][1] + covariance[2][2];
  let offDiagonal = 0;
  for (let a = 0; a < 3; a++) {
    for (let b = 0; b < 3; b++) {
      if (a !== b) offDiagonal = Math.max(offDiagonal, Math.abs(covariance[a][b]) / volume);
    }
  }
  return { volume, principal: [0, 1, 2].map((a) => (trace - covariance[a][a]) / volume), offDiagonal };
}

/** @returns {number[]} the distance from the centre to each face's plane. */
export function inradii(solid) {
  return solid.faces.map((face, i) => dot(faceNormal(solid, i), solid.vertices[face[0]]));
}

/**
 * @returns {{worst: number, involution: boolean}} how far from exactly opposed each face's
 *   nearest-to-opposite normal is, and whether that pairing pairs each face with one other.
 *   A die's numerals only mean anything if "the opposite face" is well defined.
 */
export function oppositeFaces(solid) {
  const normals = solid.faces.map((unused, i) => faceNormal(solid, i));
  const partner = normals.map((n) => {
    let best = 0;
    normals.forEach((other, i) => {
      if (dot(other, n) < dot(normals[best], n)) best = i;
    });
    return best;
  });
  return {
    worst: Math.max(...normals.map((n, i) => Math.abs(dot(normals[partner[i]], n) + 1))),
    involution: partner.every((p, i) => partner[p] === i),
  };
}

/** @returns {number[]} the distinct edge lengths, shortest first. */
export function edgeLengths(solid) {
  const seen = new Map();
  for (const face of solid.faces) {
    for (let i = 0; i < face.length; i++) {
      const [a, b] = [face[i], face[(i + 1) % face.length]];
      const key = a < b ? `${a},${b}` : `${b},${a}`;
      seen.set(key, norm(sub(solid.vertices[a], solid.vertices[b])));
    }
  }
  const lengths = [...seen.values()].sort((a, b) => a - b);
  return lengths.filter((l, i) => i === 0 || l - lengths[i - 1] > GEOMETRY_TOLERANCE);
}

/** @returns {number[]} the distinct dihedral angles in degrees, smallest first. */
export function dihedralAngles(solid) {
  const shared = new Map();
  solid.faces.forEach((face, fi) => {
    for (let i = 0; i < face.length; i++) {
      const [a, b] = [face[i], face[(i + 1) % face.length]];
      const key = a < b ? `${a},${b}` : `${b},${a}`;
      shared.set(key, [...(shared.get(key) ?? []), fi]);
    }
  });
  const angles = [...shared.values()]
    .filter((faces) => faces.length === 2)
    .map(([a, b]) => {
      const cosine = -dot(faceNormal(solid, a), faceNormal(solid, b));
      return (Math.acos(Math.min(1, Math.max(-1, cosine))) * 180) / Math.PI;
    })
    .sort((a, b) => a - b);
  return angles.filter((x, i) => i === 0 || x - angles[i - 1] > 1e-9);
}

/** @returns {object} every number the slice quotes, for a trapezohedron at parameter @p t. */
export function measure(t) {
  const solid = atCircumradius(trapezohedron(t), 1);
  const { principal, offDiagonal } = inertia(solid);
  const radii = solid.vertices.map(norm);
  const onAxis = (v) => Math.hypot(v[0], v[1]) < GEOMETRY_TOLERANCE;
  const apex = solid.vertices.filter(onAxis).map(norm);
  const ring = solid.vertices.filter((v) => !onAxis(v));
  const faceDistances = inradii(solid);
  const mean = principal.reduce((a, b) => a + b, 0) / 3;
  return {
    t,
    vertices: solid.vertices.length,
    faces: solid.faces.length,
    corners: Math.max(...solid.faces.map((f) => f.length)),
    principal,
    offDiagonal,
    inertiaRatio: principal[0] / principal[2],
    spread: Math.max(...principal.map((p) => Math.abs(p - mean))) / mean,
    inradiusSpread: Math.max(...faceDistances) - Math.min(...faceDistances),
    opposite: oppositeFaces(solid),
    apexOverRing: apex[0] / norm(ring[0]),
    heightOverWidth: (2 * apex[0]) / (2 * Math.max(...solid.vertices.map((v) => Math.hypot(v[0], v[1])))),
    edges: edgeLengths(solid),
    dihedral: dihedralAngles(solid),
    circumradius: Math.max(...radii),
  };
}

/**
 * @brief The t where @p target changes sign, by bisection.
 *
 * @throws when the bracket does not contain a sign change, rather than returning an endpoint
 *   that is not a root.
 */
export function bisect(target, low, high, steps = 200) {
  if (target(low) * target(high) > 0) {
    throw new Error(`no sign change between t=${low} and t=${high}`);
  }
  let [lo, hi] = [low, high];
  for (let i = 0; i < steps; i++) {
    const mid = (lo + hi) / 2;
    if (target(lo) * target(mid) <= 0) hi = mid;
    else lo = mid;
  }
  return (lo + hi) / 2;
}

/** @returns {number} the t whose inertia tensor is isotropic. */
export function isotropicParameter() {
  return bisect((t) => measure(t).inertiaRatio - 1, 0.1, 3);
}

// Two solids whose inertia per unit mass is known in closed form, used to prove the arithmetic
// above before any of its output is believed.
export const CHECKS = [
  {
    name: 'cube, edge 2',
    expected: 4 / 6,
    solid: () => {
      const vertices = [];
      for (const x of [-1, 1]) for (const y of [-1, 1]) for (const z of [-1, 1]) vertices.push([x, y, z]);
      const at = (x, y, z) => vertices.findIndex((v) => v[0] === x && v[1] === y && v[2] === z);
      return {
        vertices,
        faces: [
          [at(1, -1, -1), at(1, 1, -1), at(1, 1, 1), at(1, -1, 1)],
          [at(-1, -1, -1), at(-1, -1, 1), at(-1, 1, 1), at(-1, 1, -1)],
          [at(-1, 1, -1), at(-1, 1, 1), at(1, 1, 1), at(1, 1, -1)],
          [at(-1, -1, -1), at(1, -1, -1), at(1, -1, 1), at(-1, -1, 1)],
          [at(-1, -1, 1), at(1, -1, 1), at(1, 1, 1), at(-1, 1, 1)],
          [at(-1, -1, -1), at(-1, 1, -1), at(1, 1, -1), at(1, -1, -1)],
        ],
      };
    },
  },
  {
    name: 'icosahedron, edge 1',
    // The constant src/geometry/die_shape.cpp already uses for the D20.
    expected: ((1 + Math.sqrt(5)) / 2) ** 2 / 10,
    solid: () => {
      const phi = (1 + Math.sqrt(5)) / 2;
      const raw = [];
      for (const s1 of [-1, 1]) {
        for (const s2 of [-1, 1]) raw.push([0, s1, s2 * phi], [s1, s2 * phi, 0], [s1 * phi, 0, s2]);
      }
      let edge = Infinity;
      for (let i = 0; i < raw.length; i++) {
        for (let j = i + 1; j < raw.length; j++) edge = Math.min(edge, norm(sub(raw[i], raw[j])));
      }
      const vertices = raw.map((v) => scale(v, 1 / edge));
      const faces = [];
      const apart = (i, j) => Math.abs(norm(sub(vertices[i], vertices[j])) - 1) < 1e-9;
      for (let i = 0; i < 12; i++) {
        for (let j = i + 1; j < 12; j++) {
          for (let k = j + 1; k < 12; k++) {
            if (!apart(i, j) || !apart(j, k) || !apart(i, k)) continue;
            const n = cross(sub(vertices[j], vertices[i]), sub(vertices[k], vertices[i]));
            faces.push(dot(n, vertices[i]) > 0 ? [i, j, k] : [i, k, j]);
          }
        }
      }
      return { vertices, faces };
    },
  },
];

/**
 * @returns {{name: string, expected: number, got: number, error: number}[]} one row per check.
 *   A caller that finds any error above its tolerance must not believe the rest of the output.
 */
export function selfCheck() {
  return CHECKS.map(({ name, expected, solid }) => {
    const got = inertia(solid()).principal[0];
    return { name, expected, got, error: Math.abs(got - expected) };
  });
}

function main() {
  const checks = selfCheck();
  console.log('Self-check, against inertia known in closed form:');
  for (const { name, expected, got, error } of checks) {
    console.log(`  ${name.padEnd(22)} expected ${expected.toFixed(9)}  got ${got.toFixed(9)}  error ${error.toExponential(1)}`);
  }
  const worst = Math.max(...checks.map((c) => c.error));
  if (worst > GEOMETRY_TOLERANCE) {
    console.error(`self-check off by ${worst.toExponential(1)}: the numbers below are not trustworthy`);
    process.exitCode = 1;
    return;
  }

  console.log('\nThe pentagonal trapezohedron family, at circumradius 1:');
  console.log('      t   Ixx/Izz   spread  Rapex/Rring  edge long/short   height/width');
  for (const t of [0.3, 0.4, 0.5, 0.6, 0.7, 0.767463, 0.8, 0.9, 1.0, 1.3]) {
    const m = measure(t);
    console.log(
      `  ${m.t.toFixed(4)}  ${m.inertiaRatio.toFixed(4)}  ${(m.spread * 100).toFixed(2).padStart(6)}%  ` +
        `${m.apexOverRing.toFixed(4).padStart(11)}  ${(m.edges[1] / m.edges[0]).toFixed(4).padStart(15)}  ` +
        `${m.heightOverWidth.toFixed(4).padStart(13)}`,
    );
  }

  const t = isotropicParameter();
  const m = measure(t);
  console.log(`\nThe proportion whose inertia is isotropic: t = ${t.toFixed(6)}`);
  console.log(`  principal moments per mass, at circumradius 1: ${m.principal.map((p) => p.toFixed(9)).join('  ')}`);
  console.log(`  largest off-diagonal term:      ${m.offDiagonal.toExponential(2)}  (0 means these axes are principal)`);
  console.log(`  12 vertices, ${m.faces} faces, ${m.corners} corners to a face`);
  console.log(`  every face the same distance from the centre, to ${m.inradiusSpread.toExponential(2)}`);
  console.log(`  opposite faces antiparallel to ${m.opposite.worst.toExponential(2)}, pairing is an involution: ${m.opposite.involution}`);
  console.log(`  edges ${m.edges.map((e) => e.toFixed(6)).join(' and ')} of the circumradius`);
  console.log(`  dihedral angles ${m.dihedral.map((a) => a.toFixed(4)).join(' and ')} degrees`);
  console.log(`  height/width ${m.heightOverWidth.toFixed(6)}, apex/ring radius ${m.apexOverRing.toFixed(6)}`);

  const CIRCUMRADIUS_MM = 10.0;
  console.log(`\nAt the D20's circumradius of ${CIRCUMRADIUS_MM.toFixed(1)} mm, the constants the firmware needs:`);
  console.log(`  inertiaPerMass        ${(m.principal[0] * CIRCUMRADIUS_MM ** 2).toFixed(6)} mm^2`);
  console.log(`  long edge             ${(m.edges[1] * CIRCUMRADIUS_MM).toFixed(6)} mm`);
  console.log(`  short edge            ${(m.edges[0] * CIRCUMRADIUS_MM).toFixed(6)} mm`);
  // The apex is the furthest vertex, so the height is twice the circumradius by construction.
  const heightMm = 2 * CIRCUMRADIUS_MM;
  console.log(`  height x width        ${heightMm.toFixed(3)} x ${(heightMm / m.heightOverWidth).toFixed(3)} mm`);
}

// argv[1] is absent when this module is imported by something that was not started from a
// file, such as `node -e`, and converting an absent path throws rather than saying so.
if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) main();
