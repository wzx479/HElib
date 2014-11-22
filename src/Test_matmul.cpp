/* Copyright (C) 2012,2013 IBM Corp.
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */
#include <cassert>
#include <memory>
#include <NTL/lzz_pXFactoring.h>
NTL_CLIENT

#include "FHE.h"
#include "timing.h"
#include "EncryptedArray.h"

template<class type> 
class RandomMatrix : public  PlaintextMatrixInterface<type> {
public:
  PA_INJECT(type) 

private:
  const EncryptedArray& ea;

  vector< vector< RX > > data;

public:
  ~RandomMatrix() { /*cout << "destructor: random matrix\n";*/ }

  RandomMatrix(const EncryptedArray& _ea) : ea(_ea) { 
    long n = ea.size();
    long d = ea.getDegree();

    long bnd = 2*n; // non-zero with probability 1/bnd

    RBak bak; bak.save(); ea.getContext().alMod.restoreContext();

    data.resize(n);
    for (long i = 0; i < n; i++) {
      data[i].resize(n);
      for (long j = 0; j < n; j++) {
        bool zEntry = (RandomBnd(bnd) > 0);

        if (zEntry)
          clear(data[i][j]);
        else
          random(data[i][j], d);
      }
    }
  }

  virtual const EncryptedArray& getEA() const {
    return ea;
  }

  virtual bool get(RX& out, long i, long j) const {
    assert(i >= 0 && i < ea.size());
    assert(j >= 0 && j < ea.size());
    if (IsZero(data[i][j])) return true;
    out = data[i][j];
    return false;
  }
};

PlaintextMatrixBaseInterface *
buildRandomMatrix(const EncryptedArray& ea)
{
  switch (ea.getContext().alMod.getTag()) {
    case PA_GF2_tag: {
      return new RandomMatrix<PA_GF2>(ea);
    }

    case PA_zz_p_tag: {
      return new RandomMatrix<PA_zz_p>(ea);
    }

    default: return 0;
  }
}

template<class type> 
class RandomBlockMatrix : public  PlaintextBlockMatrixInterface<type> {
public:
  PA_INJECT(type) 

private:
  const EncryptedArray& ea;

  vector< vector< mat_R > > data;

public:
  ~RandomBlockMatrix() { /*cout << "destructor: random block matrix\n";*/ }
  RandomBlockMatrix(const EncryptedArray& _ea) : ea(_ea) { 
    long n = ea.size();
    long d = ea.getDegree();

    long bnd = 2*n; // non-zero with probability 1/bnd

    RBak bak; bak.save(); ea.getContext().alMod.restoreContext();

    data.resize(n);
    for (long i = 0; i < n; i++) {
      data[i].resize(n);
      for (long j = 0; j < n; j++) {
        data[i][j].SetDims(d, d);

        bool zEntry = (RandomBnd(bnd) > 0);

        for (long u = 0; u < d; u++)
          for (long v = 0; v < d; v++) 
            if (zEntry) 
              clear(data[i][j][u][v]);
            else
              random(data[i][j][u][v]);
      }
    }
  }

  virtual const EncryptedArray& getEA() const {
    return ea;
  }

  virtual bool get(mat_R& out, long i, long j) const {
    assert(i >= 0 && i < ea.size());
    assert(j >= 0 && j < ea.size());
    if (IsZero(data[i][j])) return true;
    out = data[i][j];
    return false;
  }
};


PlaintextBlockMatrixBaseInterface *
buildRandomBlockMatrix(const EncryptedArray& ea)
{
  switch (ea.getContext().alMod.getTag()) {
    case PA_GF2_tag: {
      return new RandomBlockMatrix<PA_GF2>(ea);
    }

    case PA_zz_p_tag: {
      return new RandomBlockMatrix<PA_zz_p>(ea);
    }

    default: return 0;
  }
}

void  TestIt(long p, long r, long d, long c, long k, long w, 
               long L, long m)
{
  cout << "*** TestIt: p=" << p
       << ", r=" << r
       << ", d=" << d
       << ", c=" << c
       << ", k=" << k
       << ", w=" << w
       << ", L=" << L
       << ", m=" << m
       << endl;

  FHEcontext context(m, p, r);
  buildModChain(context, L, c);

  context.zMStar.printout();
  cout << endl;

  FHESecKey secretKey(context);
  const FHEPubKey& publicKey = secretKey;
  secretKey.GenSecKey(w); // A Hamming-weight-w secret key


  ZZX G;

  if (d == 0)
    G = context.alMod.getFactorsOverZZ()[0];
  else
    G = makeIrredPoly(p, d); 

  cout << "G = " << G << "\n";
  cout << "generating key-switching matrices... ";
  addSome1DMatrices(secretKey); // compute key-switching matrices that we need
  addFrbMatrices(secretKey); // compute key-switching matrices that we need
  cout << "done\n";

  printAllTimers();
  resetAllTimers();

  cout << "computing masks and tables for rotation...";
  EncryptedArray ea(context, G);
  cout << "done\n";

  // Test a "normal" matrix over the extension field
  {
    // choose a random plaintext square matrix
    std::unique_ptr<PlaintextMatrixBaseInterface> ptr(buildRandomMatrix(ea));

    // choose a random plaintext vector
    PlaintextArray v(ea);
    v.random();

    // encrypt the random vector
    Ctxt ctxt(publicKey);
    ea.encrypt(ctxt, publicKey, v);
    Ctxt ctxt2 = ctxt;

    cout << " Multiplying with PlaintextMatrixBaseInterface... " << std::flush;
    v.mat_mul(*ptr);         // multiply the plaintext vector
    ea.mat_mul(ctxt2, *ptr);  // multiply the ciphertext vector

    PlaintextArray v1(ea);
    ea.decrypt(ctxt2, secretKey, v1); // decrypt the ciphertext vector

    if (v.equals(v1))        // check that we've got the right answer
      cout << "Nice!!\n";
    else
      cout << "Grrr@*\n";

    // Test cached verions of the mult-by-matrix operation
    {
      CachedPtxtMatrix zzxMat;
      ea.compMat(zzxMat, *ptr);
      ctxt2 = ctxt;
      cout << " Multiplying with CachedPtxtMatrix... " << std::flush;
      mat_mul(ctxt2, zzxMat, ea);
      ea.decrypt(ctxt2, secretKey, v1); // decrypt the ciphertext vector
      if (v.equals(v1))        // check that we've got the right answer
	cout << "Nice!!\n" << std::flush;
      else
	cout << "Grrr@*\n" << std::flush;
    }
    {
      CachedDCRTPtxtMatrix dcrtMat;
      ea.compMat(dcrtMat, *ptr);
      ctxt2 = ctxt;
      cout << " Multiplying with CachedDCRTPtxtMatrix... " << std::flush;
      mat_mul(ctxt2, dcrtMat, ea);
      ea.decrypt(ctxt2, secretKey, v1); // decrypt the ciphertext vector
      if (v.equals(v1))        // check that we've got the right answer
	cout << "Nice!!\n\n";
      else
	cout << "Grrr@*\n\n";
    }
  }

  // Test a "block matrix" over the base field
  {
    // choose a random plaintext square matrix
    std::unique_ptr<PlaintextBlockMatrixBaseInterface>
      ptr(buildRandomBlockMatrix(ea));

    // choose a random plaintext vector
    PlaintextArray v(ea);
    v.random();

    // encrypt the random vector
    Ctxt ctxt(publicKey);
    ea.encrypt(ctxt, publicKey, v);
    Ctxt ctxt2 = ctxt;

    v.mat_mul(*ptr);         // multiply the plaintext vector
    cout << " Multiplying with PlaintextBlockMatrixBaseInterface... " 
	 << std::flush;
    ea.mat_mul(ctxt2, *ptr);  // multiply the ciphertext vector

    PlaintextArray v1(ea);
    ea.decrypt(ctxt2, secretKey, v1); // decrypt the ciphertext vector

    if (v.equals(v1))        // check that we've got the right answer
      cout << "Nice!!\n";
    else
      cout << "Grrr...\n";

    // Test cached verion sof the mult-by-block-matrix operation
    {
      CachedPtxtBlockMatrix zzxMat;
      ea.compMat(zzxMat, *ptr);
      ctxt2 = ctxt;
      cout << " Multiplying with CachedPtxtBlockMatrix... " << std::flush;
      mat_mul(ctxt2, zzxMat, ea);
      ea.decrypt(ctxt2, secretKey, v1); // decrypt the ciphertext vector
      if (v.equals(v1))        // check that we've got the right answer
	cout << "Nice!!\n" << std::flush;
      else
	cout << "Grrr@*\n" << std::flush;
    }
    {
      CachedDCRTPtxtBlockMatrix dcrtMat;
      ea.compMat(dcrtMat, *ptr);
      ctxt2 = ctxt;
      cout << " Multiplying with CachedDCRTPtxtBlockMatrix... " << std::flush;
      mat_mul(ctxt2, dcrtMat, ea);
      ea.decrypt(ctxt2, secretKey, v1); // decrypt the ciphertext vector
      if (v.equals(v1))        // check that we've got the right answer
	cout << "Nice!!\n";
      else
	cout << "Grrr@*\n";
    }
  }
}


void usage(char *prog) 
{
  cout << "Usage: "<<prog<<" [ optional parameters ]...\n";
  cout << "  optional parameters have the form 'attr1=val1 attr2=val2 ...'\n";
  cout << "  e.g, 'p=2 k=80'\n\n";
  cout << "  p is the plaintext base [default=2]" << endl;
  cout << "  r is the lifting [default=1]" << endl;
  cout << "  d is the degree of the field extension [default==1]\n";
  cout << "    (d == 0 => factors[0] defined the extension)\n";
  cout << "  c is number of columns in the key-switching matrices [default=2]\n";
  cout << "  k is the security parameter [default=80]\n";
  cout << "  L is the # of primes in the modulus chai [default=4*R]\n";
  cout << "  s is the minimum number of slots [default=4]\n";
  cout << "  m defined the cyclotomic polynomial Phi_m(X)\n";
  exit(0);
}


int main(int argc, char *argv[]) 
{
  argmap_t argmap;
  argmap["p"] = "2";
  argmap["r"] = "1";
  argmap["d"] = "1";
  argmap["c"] = "2";
  argmap["k"] = "80";
  argmap["L"] = "3";
  argmap["s"] = "0";
  argmap["m"] = "0";

  // get parameters from the command line
  if (!parseArgs(argc, argv, argmap)) usage(argv[0]);

  long p = atoi(argmap["p"]);
  long r = atoi(argmap["r"]);
  long d = atoi(argmap["d"]);
  long c = atoi(argmap["c"]);
  long k = atoi(argmap["k"]);
  long L = atoi(argmap["L"]);
  long s = atoi(argmap["s"]);
  long chosen_m = atoi(argmap["m"]);

  long w = 64; // Hamming weight of secret key
  long m = FindM(k, L, c, p, d, s, chosen_m, true);

  //  setTimersOn();
  setTimersOn();
  TestIt(p, r, d, c, k, w, L, m);
  cout << endl;
  printAllTimers();
  cout << endl;

}

/********************************************************************/
/************               UNUSED CODE                   ***********/
/********************************************************************/
#if 0
template<class type> 
class RunningSumMatrix : public  PlaintextMatrixInterface<type> {
public:
  PA_INJECT(type) 

private:
  const EncryptedArray& ea;

public:
  ~RunningSumMatrix() { /* cout << "destructor: running sum matrix\n";*/ }

  RunningSumMatrix(const EncryptedArray& _ea) : ea(_ea) { }

  virtual const EncryptedArray& getEA() const {
    return ea;
  }

  virtual bool get(RX& out, long i, long j) const {
    assert(i >= 0 && i < ea.size());
    assert(j >= 0 && j < ea.size());
    if (j >= i)
      out = 1;
    else
      out = 0;
    return false;
  }
};


PlaintextMatrixBaseInterface *
buildRunningSumMatrix(const EncryptedArray& ea)
{
  switch (ea.getContext().alMod.getTag()) {
    case PA_GF2_tag: {
      return new RunningSumMatrix<PA_GF2>(ea);
    }

    case PA_zz_p_tag: {
      return new RunningSumMatrix<PA_zz_p>(ea);
    }

    default: return 0;
  }
}


template<class type> 
class TotalSumMatrix : public  PlaintextMatrixInterface<type> {
public:
  PA_INJECT(type) 

private:
  const EncryptedArray& ea;

public:
  ~TotalSumMatrix() { cout << "destructor: total sum matrix\n"; }

  TotalSumMatrix(const EncryptedArray& _ea) : ea(_ea) { }

  virtual const EncryptedArray& getEA() const {
    return ea;
  }

  virtual bool get(RX& out, long i, long j) const {
    assert(i >= 0 && i < ea.size());
    assert(j >= 0 && j < ea.size());
    out = 1;
    return false;
  }
};


PlaintextMatrixBaseInterface *
buildTotalSumMatrix(const EncryptedArray& ea)
{
  switch (ea.getContext().alMod.getTag()) {
    case PA_GF2_tag: {
      return new TotalSumMatrix<PA_GF2>(ea);
    }

    case PA_zz_p_tag: {
      return new TotalSumMatrix<PA_zz_p>(ea);
    }

    default: return 0;
  }
}

template<class type> 
class PolyBlockMatrix : public  PlaintextBlockMatrixInterface<type> {
public:
  PA_INJECT(type) 

private:
  const EncryptedArray& ea;

  vector< vector< mat_R > > data;

public:

  PolyBlockMatrix(const PlaintextMatrixBaseInterface& MM) : ea(MM.getEA()) { 
    RBak bak; bak.save(); ea.getContext().alMod.restoreContext();

    const PlaintextMatrixInterface<type>& M =
      dynamic_cast< const PlaintextMatrixInterface<type>& >(MM);

    long n = ea.size();
    const RX& G = ea.getDerived(type()).getG();

    data.resize(n);
    for (long i = 0; i < n; i++) {
      data[i].resize(n);
      for (long j = 0; j < n; j++) {
        RX tmp;
        if (M.get(tmp, i, j)) clear(tmp);
        matrixOfPoly(data[i][j], tmp, G);
      }
    }
  }

  virtual const EncryptedArray& getEA() const {
    return ea;
  }

  virtual bool get(mat_R& out, long i, long j) const {
    assert(i >= 0 && i < ea.size());
    assert(j >= 0 && j < ea.size());
    out = data[i][j];
    return false;
  }
};


PlaintextBlockMatrixBaseInterface *
buildPolyBlockMatrix(const PlaintextMatrixBaseInterface& M)
{
  switch (M.getEA().getContext().alMod.getTag()) {
    case PA_GF2_tag: {
      return new PolyBlockMatrix<PA_GF2>(M);
    }

    case PA_zz_p_tag: {
      return new PolyBlockMatrix<PA_zz_p>(M);
    }

    default: return 0;
  }
}

static void matrixOfPoly(mat_GF2& m, const GF2X& a, const GF2X& f)
{
  long d = deg(f);
  m.SetDims(d, d);
  GF2X b = a;

  for (long i = 0; i < d; i++) {
    VectorCopy(m[i], b, d);
    MulByXMod(b, b, f);
  }
}

static void matrixOfPoly(mat_zz_p& m, const zz_pX& a, const zz_pX& f)
{
  long d = deg(f);
  m.SetDims(d, d);
  zz_pX b = a;

  for (long i = 0; i < d; i++) {
    VectorCopy(m[i], b, d);
    MulByXMod(b, b, f);
  }
}
#endif
