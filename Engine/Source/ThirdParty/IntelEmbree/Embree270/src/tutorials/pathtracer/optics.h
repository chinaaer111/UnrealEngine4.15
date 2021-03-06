// ======================================================================== //
// Copyright 2009-2015 Intel Corporation                                    //
//                                                                          //
// Licensed under the Apache License, Version 2.0 (the "License");          //
// you may not use this file except in compliance with the License.         //
// You may obtain a copy of the License at                                  //
//                                                                          //
//     http://www.apache.org/licenses/LICENSE-2.0                           //
//                                                                          //
// Unless required by applicable law or agreed to in writing, software      //
// distributed under the License is distributed on an "AS IS" BASIS,        //
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. //
// See the License for the specific language governing permissions and      //
// limitations under the License.                                           //
// ======================================================================== //

#pragma once

/*! \addtogroup rivl_render_embree_ivl */
/*! @{ */

/*! Reflects a viewing vector V at a normal N. */
inline Sample3f reflect_(const Vec3fa &V, const Vec3fa &N) {
  float cosi = dot(V,N);
  return Sample3f(2.0f*cosi*N-V, 1.0f);
}

/*! Reflects a viewing vector V at a normal N. Cosine between V
 *  and N is given as input. */
inline Sample3f reflect_(const Vec3fa &V, const Vec3fa &N, const float cosi) {
  return Sample3f(2.0f*cosi*N-V, 1.0f);
}

// =======================================================
/*!Refracts a viewing vector V at a normal N using the relative
 *  refraction index eta. Eta is refraction index of outside medium
 *  (where N points into) divided by refraction index of the inside
 *  medium. The vectors V and N have to point towards the same side
 *  of the surface. The cosine between V and N is given as input and
 *  the cosine of -N and transmission ray is computed as output. */
inline Sample3f refract(const Vec3fa& V, const Vec3fa& N, const float eta, 
                        const float cosi, float &cost)
{
  const float k = 1.0f-eta*eta*(1.0f-cosi*cosi);
  if (k < 0.0f) { cost = 0.0f; return Sample3f(Vec3fa(0.f),0.0f); }
  cost = sqrt(k);
  return Sample3f(eta*(cosi*N-V)-cost*N, sqr(eta));
}

/*! Computes fresnel coefficient for media interface with relative
 *  refraction index eta. Eta is the outside refraction index
 *  divided by the inside refraction index. Both cosines have to be
 *  positive. */
inline float fresnelDielectric(const float cosi, const float cost, const float eta)
{
  const float Rper = (eta*cosi -     cost) * rcp(eta*cosi +     cost);
  const float Rpar = (    cosi - eta*cost) * rcp(    cosi + eta*cost);
  return 0.5f*(Rpar*Rpar + Rper*Rper);
}

  /*! Computes fresnel coefficient for media interface with relative
   *  refraction index eta. Eta is the outside refraction index
   *  divided by the inside refraction index. The cosine has to be
   *  positive. */
inline float fresnelDielectric(const float cosi, const float eta)
{
  const float k = 1.0f-eta*eta*(1.0f-cosi*cosi);
  if (k < 0.0f) return 1.0f;
  const float cost = sqrt(k);
  return fresnelDielectric(cosi, cost, eta);
}

/*! Computes fresnel coefficient for conductor medium with complex
 *  refraction index (eta,k). The cosine has to be positive. */
inline Vec3fa fresnelConductor(const float cosi, const Vec3fa& eta, const Vec3fa& k)
{
  const Vec3fa tmp = eta*eta + k*k;
  const Vec3fa Rpar = (tmp * (cosi*cosi) - 2.0f*eta*cosi + Vec3fa(1.0f)) *
    rcp(tmp * (cosi*cosi) + 2.0f*eta*cosi + Vec3fa(1.0f));
  const Vec3fa Rper = (tmp - 2.0f*eta*cosi + Vec3fa(cosi*cosi)) *
    rcp(tmp + 2.0f*eta*cosi + Vec3fa(cosi*cosi));
  return 0.5f * (Rpar + Rper);
}

// =======================================================
struct FresnelConductor {
  Vec3fa eta;  //!< Real part of refraction index
  Vec3fa k;    //!< Imaginary part of refraction index
};

inline Vec3fa eval(const FresnelConductor& This, const float cosTheta) {
  return fresnelConductor(cosTheta,This.eta,This.k);
}

inline FresnelConductor make_FresnelConductor(const Vec3fa& eta, const Vec3fa& k) {
  FresnelConductor m; m.eta = eta; m.k = k; return m;
}

#if defined(ISPC)
inline FresnelConductor make_FresnelConductor(const Vec3fa& eta, const Vec3fa& k) {
  FresnelConductor m; m.eta = eta; m.k = k; return m;
}
#endif

// =======================================================
struct FresnelDielectric 
{
  /*! refraction index of the medium the incident ray travels in */
  float etai;
  
  /*! refraction index of the medium the outgoing transmission rays
   *  travels in */
  float etat;
};

inline Vec3fa eval(const FresnelDielectric& This, const float cosTheta) {
  return Vec3fa(fresnelDielectric(cosTheta,This.etai/This.etat));
}

inline FresnelDielectric make_FresnelDielectric(const float etai, const float etat) {
  FresnelDielectric m; m.etai = etai; m.etat = etat; return m;
}

#if defined(ISPC)
inline FresnelDielectric make_FresnelDielectric(const float etai, const float etat) {
  FresnelDielectric m; m.etai = etai; m.etat = etat; return m;
}
#endif

// =======================================================
struct PowerCosineDistribution {
  float exp;
};

inline float eval(const PowerCosineDistribution &This, const float cosThetaH) {
  return (This.exp+2) * (1.0f/(2.0f*(float(pi)))) * pow(abs(cosThetaH), This.exp);
}

#if defined(ISPC)

inline float eval(const PowerCosineDistribution &This, const float cosThetaH) {
  return (This.exp+2) * (1.0f/(2.0f*(float(pi)))) * pow(abs(cosThetaH), This.exp);
}
#endif

/*! Samples the power cosine distribution. */
inline void sample(const PowerCosineDistribution& This, const Vec3fa& wo, const Vec3fa& N, Sample3f &wi, const Vec2f s)  
{
  Sample3f wh = powerCosineSampleHemisphere(s.x,s.y,N,This.exp);
  Sample3f r = reflect_(wo,wh.v);
  wi = Sample3f(r.v,wh.pdf/(4.0f*abs(dot(wo,wh.v))));
}

/*! Samples the power cosine distribution. */
#if defined(ISPC)
inline void sample(const PowerCosineDistribution& This, const Vec3fa& wo, const Vec3fa& N, Sample3f &wi, const Vec2f s)  
{
  Sample3f wh = powerCosineSampleHemisphere(s.x,s.y,N,This.exp);
  Sample3f r = reflect_(wo,wh.v);
  wi = Sample3f(r.v,wh.pdf/(4.0f*abs(dot(wo,wh.v))));
}
#endif

inline PowerCosineDistribution make_PowerCosineDistribution(const float _exp) { 
  PowerCosineDistribution m; m.exp = _exp; return m;
}

#if defined(ISPC)
inline PowerCosineDistribution make_PowerCosineDistribution(const float _exp) { 
  PowerCosineDistribution m; m.exp = _exp; return m;
}
#endif

/*! @} */
