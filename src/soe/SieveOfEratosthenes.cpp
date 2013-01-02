///
/// @file   SieveOfEratosthenes.cpp
/// @brief  Implementation of the segmented sieve of Eratosthenes.
///
/// Copyright (C) 2013 Kim Walisch, <kim.walisch@gmail.com>
///
/// This file is distributed under the New BSD License. See the
/// LICENSE file in the top level directory.
///

#include "config.h"
#include "SieveOfEratosthenes.h"
#include "PreSieve.h"
#include "EratSmall.h"
#include "EratMedium.h"
#include "EratBig.h"
#include "imath.h"
#include "primesieve_error.h"

#include <stdint.h>
#include <cstdlib>

namespace soe {

const uint_t SieveOfEratosthenes::bitValues_[8] = { 7, 11, 13, 17, 19, 23, 29, 31 };

/// De Bruijn bitscan table
const uint_t SieveOfEratosthenes::bruijnBitValues_[64] =
{
    7,  47,  11,  49,  67, 113,  13,  53,
   89,  71, 161, 101, 119, 187,  17, 233,
   59,  79,  91,  73, 133, 139, 163, 103,
  149, 121, 203, 169, 191, 217,  19, 239,
   43,  61, 109,  83, 157,  97, 181, 229,
   77, 131, 137, 143, 199, 167, 211,  41,
  107, 151, 179, 227, 127, 197, 209,  37,
  173, 223, 193,  31, 221,  29,  23, 241
};

/// @param start      Sieve primes >= start.
/// @param stop       Sieve primes <= stop.
/// @param sieveSize  A sieve size in kilobytes.
/// @param preSieve   Pre-sieve multiples of small primes <= preSieve
///                   to speed up the sieve of Eratosthenes.
/// @pre   start      >= 7
/// @pre   stop       <= 2^64 - 2^32 * 10
/// @pre   sieveSize  >= 1 && <= 4096
/// @pre   preSieve   >= 13 && <= 23
///
SieveOfEratosthenes::SieveOfEratosthenes(uint64_t start,
                                         uint64_t stop,
                                         uint_t sieveSize,
                                         uint_t preSieve) :
  start_(start),
  stop_(stop),
  sqrtStop_(static_cast<uint_t>(isqrt(stop))),
  preSieve_(preSieve),
  eratSmall_(NULL),
  eratMedium_(NULL),
  eratBig_(NULL)
{
  if (start_ < 7)
    throw primesieve_error("SieveOfEratosthenes: start must be >= 7");
  if (start_ > stop_)
    throw primesieve_error("SieveOfEratosthenes: start must be <= stop");
  // sieveSize_ must be a power of 2
  sieveSize_ = getInBetween(1, floorPowerOf2<int>(sieveSize), 4096);
  sieveSize_ *= 1024; // convert to bytes
  segmentLow_ = start_ - getByteRemainder(start_);
  segmentHigh_ = segmentLow_ + sieveSize_ * NUMBERS_PER_BYTE + 1;
  initEratAlgorithms();
  // allocate the sieve of Eratosthenes array
  sieve_ = new uint8_t[sieveSize_];
}

SieveOfEratosthenes::~SieveOfEratosthenes() {
  delete eratSmall_;
  delete eratMedium_;
  delete eratBig_;
  delete[] sieve_;
}

void SieveOfEratosthenes::initEratAlgorithms() {
  uint_t esLimit = static_cast<uint_t>(sieveSize_ * config::FACTOR_ERATSMALL);
  uint_t emLimit = static_cast<uint_t>(sieveSize_ * config::FACTOR_ERATMEDIUM);
  try {
    if (sqrtStop_ > preSieve_.getLimit()) {
      eratSmall_ = new EratSmall(stop_, sieveSize_, esLimit);
      if (sqrtStop_ > eratSmall_->getLimit()) {
        eratMedium_ = new EratMedium(stop_, sieveSize_, emLimit);
        if (sqrtStop_ > eratMedium_->getLimit())
          eratBig_ = new EratBig(stop_, sieveSize_, sqrtStop_);
      }
    }
  } catch (...) {
    delete eratSmall_;
    delete eratMedium_;
    delete eratBig_;
    throw;
  }
}

uint64_t SieveOfEratosthenes::getByteRemainder(uint64_t n) {
  uint64_t remainder = n % NUMBERS_PER_BYTE;
  if (remainder <= 1)
    remainder += NUMBERS_PER_BYTE;
  return remainder;
}

/// Pre-sieve multiples of small primes e.g. <= 19
/// to speed up the sieve of Eratosthenes.
///
void SieveOfEratosthenes::preSieve() {
  preSieve_.doIt(sieve_, sieveSize_, segmentLow_);
  // unset bits (numbers) < start_
  if (segmentLow_ <= start_) {
    if (start_ <= preSieve_.getLimit())
      sieve_[0] = 0xff;
    int i = 0;
    while (bitValues_[i] < getByteRemainder(start_)) i++;
    sieve_[0] &= 0xff << i;
  }
}

void SieveOfEratosthenes::crossOffMultiples() {
  if (eratSmall_ != NULL) {
    // process small sieving primes with many multiples per segment
    eratSmall_->crossOff(sieve_, &sieve_[sieveSize_]);
    if (eratMedium_ != NULL) {
      // process medium sieving primes with a few multiples per segment
      eratMedium_->crossOff(sieve_, sieveSize_);
      if (eratBig_ != NULL)
        // process big sieving primes with very few ...
        eratBig_->crossOff(sieve_);
    }
  }
}

void SieveOfEratosthenes::sieveSegment() {
  preSieve();
  crossOffMultiples();
  segmentProcessed(sieve_, sieveSize_);
}

/// Sieve the last segments remaining after that sieve(uint_t)
/// has been called for all primes up to sqrt(stop).
///
void SieveOfEratosthenes::finish() {
  // sieve all segments left except the last one
  while (segmentHigh_ < stop_) {
    sieveSegment();
    segmentLow_  += sieveSize_ * NUMBERS_PER_BYTE;
    segmentHigh_ += sieveSize_ * NUMBERS_PER_BYTE;
  }
  // sieve the last segment
  uint64_t remainder = getByteRemainder(stop_);
  sieveSize_ = static_cast<uint_t>((stop_ - remainder) - segmentLow_) / NUMBERS_PER_BYTE + 1;
  segmentHigh_ = segmentLow_ + sieveSize_ * NUMBERS_PER_BYTE + 1;
  preSieve();
  crossOffMultiples();
  int i;
  // unset bits (numbers) > stop_
  for (i = 0; i < 8; i++)
    if (bitValues_[i] > remainder)
      break;
  sieve_[sieveSize_ - 1] &= ~(0xff << i);
  for (uint_t j = sieveSize_; j % 8 != 0; j++)
    sieve_[j] = 0;
  segmentProcessed(sieve_, sieveSize_);
}

} // namespace soe
