// donna_64.cpp - written and placed in public domain by Jeffrey Walton
//                This is a port of Adam Langley's curve25519-donna
//                located at https://github.com/agl/curve25519-donna

/* Copyright 2008, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * curve25519-donna: Curve25519 elliptic curve, public key function
 *
 * http://code.google.com/p/curve25519-donna/
 *
 * Adam Langley <agl@imperialviolet.org>
 *
 * Derived from public domain C code by Daniel J. Bernstein <djb@cr.yp.to>
 *
 * More information about curve25519 can be found here
 *   http://cr.yp.to/ecdh.html
 *
 * djb's sample implementation of curve25519 is written in a special assembly
 * language called qhasm and uses the floating point registers.
 *
 * This is, almost, a clean room reimplementation from the curve25519 paper. It
 * uses many of the tricks described therein. Only the crecip function is taken
 * from the sample implementation. */

#include "pch.h"

#include "config.h"
#include "donna.h"
#include "stdcpp.h"

// This macro is not in a header like config.h because
// we don't want it exposed to user code. We also need
// a standard header like <stdint.h> or <stdef.h>.
// Langley uses uint128_t in the 64-bit code paths so
// we further restrict 64-bit code.
#if (UINTPTR_MAX == 0xffffffff) || !defined(CRYPTOPP_WORD128_AVAILABLE)
# define CRYPTOPP_32BIT 1
#else
# define CRYPTOPP_64BIT 1
#endif

// Squash MS LNK4221 and libtool warnings
extern const char DONNA64_FNAME[] = __FILE__;

#if defined(CRYPTOPP_64BIT)

ANONYMOUS_NAMESPACE_BEGIN

using std::memcpy;
using CryptoPP::byte;
using CryptoPP::word32;
using CryptoPP::word64;
using CryptoPP::sword32;
using CryptoPP::sword64;
using CryptoPP::word128;

typedef word64 limb;
typedef limb felem[5];

// This is a special gcc mode for 128-bit integers. It's implemented on 64-bit
// platforms only as far as I know.
//typedef unsigned uint128_t __attribute__((mode(TI)));

/* Sum two numbers: output += in */
inline void fsum(limb *output, const limb *in)
{
  output[0] += in[0];
  output[1] += in[1];
  output[2] += in[2];
  output[3] += in[3];
  output[4] += in[4];
}

/* Find the difference of two numbers: output = in - output
 * (note the order of the arguments!)
 *
 * Assumes that out[i] < 2**52
 * On return, out[i] < 2**55
 */
inline void fdifference_backwards(felem out, const felem in)
{
  /* 152 is 19 << 3 */
  const limb two54m152 = (((limb)1) << 54) - 152;
  const limb two54m8 = (((limb)1) << 54) - 8;

  out[0] = in[0] + two54m152 - out[0];
  out[1] = in[1] + two54m8 - out[1];
  out[2] = in[2] + two54m8 - out[2];
  out[3] = in[3] + two54m8 - out[3];
  out[4] = in[4] + two54m8 - out[4];
}

/* Multiply a number by a scalar: output = in * scalar */
inline void fscalar_product(felem output, const felem in, const limb scalar)
{
  word128 a;

  a = ((word128) in[0]) * scalar;
  output[0] = ((limb)a) & 0x7ffffffffffff;

  a = ((word128) in[1]) * scalar + ((limb) (a >> 51));
  output[1] = ((limb)a) & 0x7ffffffffffff;

  a = ((word128) in[2]) * scalar + ((limb) (a >> 51));
  output[2] = ((limb)a) & 0x7ffffffffffff;

  a = ((word128) in[3]) * scalar + ((limb) (a >> 51));
  output[3] = ((limb)a) & 0x7ffffffffffff;

  a = ((word128) in[4]) * scalar + ((limb) (a >> 51));
  output[4] = ((limb)a) & 0x7ffffffffffff;

  output[0] += (a >> 51) * 19;
}

/* Multiply two numbers: output = in2 * in
 *
 * output must be distinct to both inputs. The inputs are reduced coefficient
 * form, the output is not.
 *
 * Assumes that in[i] < 2**55 and likewise for in2.
 * On return, output[i] < 2**52
 */
inline void fmul(felem output, const felem in2, const felem in)
{
  word128 t[5];
  limb r0,r1,r2,r3,r4,s0,s1,s2,s3,s4,c;

  r0 = in[0];
  r1 = in[1];
  r2 = in[2];
  r3 = in[3];
  r4 = in[4];

  s0 = in2[0];
  s1 = in2[1];
  s2 = in2[2];
  s3 = in2[3];
  s4 = in2[4];

  t[0]  =  ((word128) r0) * s0;
  t[1]  =  ((word128) r0) * s1 + ((word128) r1) * s0;
  t[2]  =  ((word128) r0) * s2 + ((word128) r2) * s0 + ((word128) r1) * s1;
  t[3]  =  ((word128) r0) * s3 + ((word128) r3) * s0 + ((word128) r1) * s2 + ((word128) r2) * s1;
  t[4]  =  ((word128) r0) * s4 + ((word128) r4) * s0 + ((word128) r3) * s1 + ((word128) r1) * s3 + ((word128) r2) * s2;

  r4 *= 19;
  r1 *= 19;
  r2 *= 19;
  r3 *= 19;

  t[0] += ((word128) r4) * s1 + ((word128) r1) * s4 + ((word128) r2) * s3 + ((word128) r3) * s2;
  t[1] += ((word128) r4) * s2 + ((word128) r2) * s4 + ((word128) r3) * s3;
  t[2] += ((word128) r4) * s3 + ((word128) r3) * s4;
  t[3] += ((word128) r4) * s4;

                  r0 = (limb)t[0] & 0x7ffffffffffff; c = (limb)(t[0] >> 51);
  t[1] += c;      r1 = (limb)t[1] & 0x7ffffffffffff; c = (limb)(t[1] >> 51);
  t[2] += c;      r2 = (limb)t[2] & 0x7ffffffffffff; c = (limb)(t[2] >> 51);
  t[3] += c;      r3 = (limb)t[3] & 0x7ffffffffffff; c = (limb)(t[3] >> 51);
  t[4] += c;      r4 = (limb)t[4] & 0x7ffffffffffff; c = (limb)(t[4] >> 51);
  r0 +=   c * 19; c = r0 >> 51; r0 = r0 & 0x7ffffffffffff;
  r1 +=   c;      c = r1 >> 51; r1 = r1 & 0x7ffffffffffff;
  r2 +=   c;

  output[0] = r0;
  output[1] = r1;
  output[2] = r2;
  output[3] = r3;
  output[4] = r4;
}

inline void fsquare_times(felem output, const felem in, limb count)
{
  word128 t[5];
  limb r0,r1,r2,r3,r4,c;
  limb d0,d1,d2,d4,d419;

  r0 = in[0];
  r1 = in[1];
  r2 = in[2];
  r3 = in[3];
  r4 = in[4];

  do {
    d0 = r0 * 2;
    d1 = r1 * 2;
    d2 = r2 * 2 * 19;
    d419 = r4 * 19;
    d4 = d419 * 2;

    t[0] = ((word128) r0) * r0 + ((word128) d4) * r1 + (((word128) d2) * (r3     ));
    t[1] = ((word128) d0) * r1 + ((word128) d4) * r2 + (((word128) r3) * (r3 * 19));
    t[2] = ((word128) d0) * r2 + ((word128) r1) * r1 + (((word128) d4) * (r3     ));
    t[3] = ((word128) d0) * r3 + ((word128) d1) * r2 + (((word128) r4) * (d419   ));
    t[4] = ((word128) d0) * r4 + ((word128) d1) * r3 + (((word128) r2) * (r2     ));

                    r0 = (limb)t[0] & 0x7ffffffffffff; c = (limb)(t[0] >> 51);
    t[1] += c;      r1 = (limb)t[1] & 0x7ffffffffffff; c = (limb)(t[1] >> 51);
    t[2] += c;      r2 = (limb)t[2] & 0x7ffffffffffff; c = (limb)(t[2] >> 51);
    t[3] += c;      r3 = (limb)t[3] & 0x7ffffffffffff; c = (limb)(t[3] >> 51);
    t[4] += c;      r4 = (limb)t[4] & 0x7ffffffffffff; c = (limb)(t[4] >> 51);
    r0 +=   c * 19; c = r0 >> 51; r0 = r0 & 0x7ffffffffffff;
    r1 +=   c;      c = r1 >> 51; r1 = r1 & 0x7ffffffffffff;
    r2 +=   c;
  } while(--count);

  output[0] = r0;
  output[1] = r1;
  output[2] = r2;
  output[3] = r3;
  output[4] = r4;
}

/* Load a little-endian 64-bit number  */
limb load_limb(const byte *in)
{
  return
    ((limb)in[0]) |
    (((limb)in[1]) << 8) |
    (((limb)in[2]) << 16) |
    (((limb)in[3]) << 24) |
    (((limb)in[4]) << 32) |
    (((limb)in[5]) << 40) |
    (((limb)in[6]) << 48) |
    (((limb)in[7]) << 56);
}

void store_limb(byte *out, limb in)
{
  out[0] = in & 0xff;
  out[1] = (in >> 8) & 0xff;
  out[2] = (in >> 16) & 0xff;
  out[3] = (in >> 24) & 0xff;
  out[4] = (in >> 32) & 0xff;
  out[5] = (in >> 40) & 0xff;
  out[6] = (in >> 48) & 0xff;
  out[7] = (in >> 56) & 0xff;
}

/* Take a little-endian, 32-byte number and expand it into polynomial form */
void fexpand(limb *output, const byte *in)
{
  output[0] = load_limb(in) & 0x7ffffffffffff;
  output[1] = (load_limb(in+6) >> 3) & 0x7ffffffffffff;
  output[2] = (load_limb(in+12) >> 6) & 0x7ffffffffffff;
  output[3] = (load_limb(in+19) >> 1) & 0x7ffffffffffff;
  output[4] = (load_limb(in+24) >> 12) & 0x7ffffffffffff;
}

/* Take a fully reduced polynomial form number and contract it into a
 * little-endian, 32-byte array
 */
void fcontract(byte *output, const felem input)
{
  word128 t[5];

  t[0] = input[0];
  t[1] = input[1];
  t[2] = input[2];
  t[3] = input[3];
  t[4] = input[4];

  t[1] += t[0] >> 51; t[0] &= 0x7ffffffffffff;
  t[2] += t[1] >> 51; t[1] &= 0x7ffffffffffff;
  t[3] += t[2] >> 51; t[2] &= 0x7ffffffffffff;
  t[4] += t[3] >> 51; t[3] &= 0x7ffffffffffff;
  t[0] += 19 * (t[4] >> 51); t[4] &= 0x7ffffffffffff;

  t[1] += t[0] >> 51; t[0] &= 0x7ffffffffffff;
  t[2] += t[1] >> 51; t[1] &= 0x7ffffffffffff;
  t[3] += t[2] >> 51; t[2] &= 0x7ffffffffffff;
  t[4] += t[3] >> 51; t[3] &= 0x7ffffffffffff;
  t[0] += 19 * (t[4] >> 51); t[4] &= 0x7ffffffffffff;

  /* now t is between 0 and 2^255-1, properly carried. */
  /* case 1: between 0 and 2^255-20. case 2: between 2^255-19 and 2^255-1. */

  t[0] += 19;

  t[1] += t[0] >> 51; t[0] &= 0x7ffffffffffff;
  t[2] += t[1] >> 51; t[1] &= 0x7ffffffffffff;
  t[3] += t[2] >> 51; t[2] &= 0x7ffffffffffff;
  t[4] += t[3] >> 51; t[3] &= 0x7ffffffffffff;
  t[0] += 19 * (t[4] >> 51); t[4] &= 0x7ffffffffffff;

  /* now between 19 and 2^255-1 in both cases, and offset by 19. */

  t[0] += 0x8000000000000 - 19;
  t[1] += 0x8000000000000 - 1;
  t[2] += 0x8000000000000 - 1;
  t[3] += 0x8000000000000 - 1;
  t[4] += 0x8000000000000 - 1;

  /* now between 2^255 and 2^256-20, and offset by 2^255. */

  t[1] += t[0] >> 51; t[0] &= 0x7ffffffffffff;
  t[2] += t[1] >> 51; t[1] &= 0x7ffffffffffff;
  t[3] += t[2] >> 51; t[2] &= 0x7ffffffffffff;
  t[4] += t[3] >> 51; t[3] &= 0x7ffffffffffff;
  t[4] &= 0x7ffffffffffff;

  store_limb(output,    t[0] | (t[1] << 51));
  store_limb(output+8,  (t[1] >> 13) | (t[2] << 38));
  store_limb(output+16, (t[2] >> 26) | (t[3] << 25));
  store_limb(output+24, (t[3] >> 39) | (t[4] << 12));
}

/* Input: Q, Q', Q-Q'
 * Output: 2Q, Q+Q'
 *
 *   x2 z3: long form
 *   x3 z3: long form
 *   x z: short form, destroyed
 *   xprime zprime: short form, destroyed
 *   qmqp: short form, preserved
 */
void fmonty(limb *x2, limb *z2, /* output 2Q */
            limb *x3, limb *z3, /* output Q + Q' */
            limb *x, limb *z,   /* input Q */
            limb *xprime, limb *zprime, /* input Q' */
            const limb *qmqp /* input Q - Q' */)
{
  limb origx[5], origxprime[5], zzz[5], xx[5], zz[5];
  limb xxprime[5], zzprime[5], zzzprime[5];

  memcpy(origx, x, 5 * sizeof(limb));
  fsum(x, z);
  fdifference_backwards(z, origx);  // does x - z

  memcpy(origxprime, xprime, sizeof(limb) * 5);
  fsum(xprime, zprime);
  fdifference_backwards(zprime, origxprime);
  fmul(xxprime, xprime, z);
  fmul(zzprime, x, zprime);
  memcpy(origxprime, xxprime, sizeof(limb) * 5);
  fsum(xxprime, zzprime);
  fdifference_backwards(zzprime, origxprime);
  fsquare_times(x3, xxprime, 1);
  fsquare_times(zzzprime, zzprime, 1);
  fmul(z3, zzzprime, qmqp);

  fsquare_times(xx, x, 1);
  fsquare_times(zz, z, 1);
  fmul(x2, xx, zz);
  fdifference_backwards(zz, xx);  // does zz = xx - zz
  fscalar_product(zzz, zz, 121665);
  fsum(zzz, xx);
  fmul(z2, zz, zzz);
}

// -----------------------------------------------------------------------------
// Maybe swap the contents of two limb arrays (@a and @b), each @len elements
// long. Perform the swap iff @swap is non-zero.
//
// This function performs the swap without leaking any side-channel
// information.
// -----------------------------------------------------------------------------
void swap_conditional(limb a[5], limb b[5], limb iswap)
{
  const limb swap = -iswap;

  for (unsigned int i = 0; i < 5; ++i) {
    const limb x = swap & (a[i] ^ b[i]);
    a[i] ^= x;
    b[i] ^= x;
  }
}

/* Calculates nQ where Q is the x-coordinate of a point on the curve
 *
 *   resultx/resultz: the x coordinate of the resulting curve point (short form)
 *   n: a little endian, 32-byte number
 *   q: a point of the curve (short form)
 */
void cmult(limb *resultx, limb *resultz, const byte *n, const limb *q)
{
  limb a[5] = {0}, b[5] = {1}, c[5] = {1}, d[5] = {0};
  limb *nqpqx = a, *nqpqz = b, *nqx = c, *nqz = d, *t;
  limb e[5] = {0}, f[5] = {1}, g[5] = {0}, h[5] = {1};
  limb *nqpqx2 = e, *nqpqz2 = f, *nqx2 = g, *nqz2 = h;

  memcpy(nqpqx, q, sizeof(limb) * 5);

  for (unsigned int i = 0; i < 32; ++i) {
    byte b = n[31 - i];
    for (unsigned int j = 0; j < 8; ++j) {
      const limb bit = b >> 7;

      swap_conditional(nqx, nqpqx, bit);
      swap_conditional(nqz, nqpqz, bit);
      fmonty(nqx2, nqz2,
             nqpqx2, nqpqz2,
             nqx, nqz,
             nqpqx, nqpqz,
             q);
      swap_conditional(nqx2, nqpqx2, bit);
      swap_conditional(nqz2, nqpqz2, bit);

      t = nqx;
      nqx = nqx2;
      nqx2 = t;
      t = nqz;
      nqz = nqz2;
      nqz2 = t;
      t = nqpqx;
      nqpqx = nqpqx2;
      nqpqx2 = t;
      t = nqpqz;
      nqpqz = nqpqz2;
      nqpqz2 = t;

      b <<= 1;
    }
  }

  memcpy(resultx, nqx, sizeof(limb) * 5);
  memcpy(resultz, nqz, sizeof(limb) * 5);
}


// -----------------------------------------------------------------------------
// Shamelessly copied from djb's code, tightened a little
// -----------------------------------------------------------------------------
void crecip(felem out, const felem z)
{
  felem a,t0,b,c;

  /* 2 */ fsquare_times(a, z, 1); // a = 2
  /* 8 */ fsquare_times(t0, a, 2);
  /* 9 */ fmul(b, t0, z); // b = 9
  /* 11 */ fmul(a, b, a); // a = 11
  /* 22 */ fsquare_times(t0, a, 1);
  /* 2^5 - 2^0 = 31 */ fmul(b, t0, b);
  /* 2^10 - 2^5 */ fsquare_times(t0, b, 5);
  /* 2^10 - 2^0 */ fmul(b, t0, b);
  /* 2^20 - 2^10 */ fsquare_times(t0, b, 10);
  /* 2^20 - 2^0 */ fmul(c, t0, b);
  /* 2^40 - 2^20 */ fsquare_times(t0, c, 20);
  /* 2^40 - 2^0 */ fmul(t0, t0, c);
  /* 2^50 - 2^10 */ fsquare_times(t0, t0, 10);
  /* 2^50 - 2^0 */ fmul(b, t0, b);
  /* 2^100 - 2^50 */ fsquare_times(t0, b, 50);
  /* 2^100 - 2^0 */ fmul(c, t0, b);
  /* 2^200 - 2^100 */ fsquare_times(t0, c, 100);
  /* 2^200 - 2^0 */ fmul(t0, t0, c);
  /* 2^250 - 2^50 */ fsquare_times(t0, t0, 50);
  /* 2^250 - 2^0 */ fmul(t0, t0, b);
  /* 2^255 - 2^5 */ fsquare_times(t0, t0, 5);
  /* 2^255 - 21 */ fmul(out, t0, a);
}

ANONYMOUS_NAMESPACE_END

NAMESPACE_BEGIN(CryptoPP)
NAMESPACE_BEGIN(Donna)

int curve25519(byte pubkey[32], const byte seckey[32], const byte basepoint[32])
{
  limb bp[5], x[5], z[5], zmone[5];
  uint8_t e[32];
  int i;

  for (i = 0;i < 32;++i)
    e[i] = seckey[i];

  e[0] &= 248;
  e[31] &= 127;
  e[31] |= 64;

  fexpand(bp, basepoint);
  cmult(x, z, e, bp);
  crecip(zmone, z);
  fmul(z, x, zmone);
  fcontract(pubkey, z);
  return 0;
}

NAMESPACE_END  // Donna
NAMESPACE_END  // CryptoPP

#endif  // CRYPTOPP_64BIT
