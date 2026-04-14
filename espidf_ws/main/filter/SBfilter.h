/******************************* SOURCE LICENSE *********************************
Copyright (c) 2021 MicroModeler.

A non-exclusive, nontransferable, perpetual, royalty-free license is granted to the Licensee to
use the following Information for academic, non-profit, or government-sponsored research purposes.
Use of the following Information under this License is restricted to NON-COMMERCIAL PURPOSES ONLY.
Commercial use of the following Information requires a separately executed written license agreement.

This Information is distributed WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

******************************* END OF LICENSE *********************************/

// A commercial license for MicroModeler DSP can be obtained at https://www.micromodeler.com/launch.jsp

// Begin header file, SBfilter.h

#ifndef SBFILTER_H_ // Include guards
#define SBFILTER_H_

/*
Generated code is based on the following filter design:
<micro.DSP.FilterDocument sampleFrequency="#500" arithmetic="float" biquads="Direct1" classname="SBfilter" inputMax="#1" inputShift="#-1" >
  <micro.DSP.IirButterworthFilter N="#4" bandType="s" w1="#0.099" w2="#0.101" stopbandRipple="#undefined" passbandRipple="#undefined" transitionRatio="#undefined" >
    <micro.DSP.FilterStructure coefficientBits="#0" variableBits="#0" accumulatorBits="#0" biquads="Direct1" >
      <micro.DSP.FilterSection form="Direct1" historyType="Double" accumulatorBits="#0" variableBits="#0" coefficientBits="#0" />
      <micro.DSP.FilterSection form="Direct1" historyType="Double" accumulatorBits="#0" variableBits="#0" coefficientBits="#0" />
      <micro.DSP.FilterSection form="Direct1" historyType="Double" accumulatorBits="#0" variableBits="#0" coefficientBits="#0" />
      <micro.DSP.FilterSection form="Direct1" historyType="Double" accumulatorBits="#0" variableBits="#0" coefficientBits="#0" />
    </micro.DSP.FilterStructure>
    <micro.DSP.PoleOrZeroContainer >
      <micro.DSP.PoleOrZero i="#0.5816835108446754" r="#0.8104845613629102" isPoint="#true" isPole="#true" isZero="#false" symmetry="c" N="#1" cascade="#3" />
      <micro.DSP.PoleOrZero i="#0.5910310342330917" r="#0.803645923525924" isPoint="#true" isPole="#true" isZero="#false" symmetry="c" N="#1" cascade="#2" />
      <micro.DSP.PoleOrZero i="#0.582421080126636" r="#0.8057796090640543" isPoint="#true" isPole="#true" isZero="#false" symmetry="c" N="#1" cascade="#1" />
      <micro.DSP.PoleOrZero i="#0.5862669218739487" r="#0.8029384908544951" isPoint="#true" isPole="#true" isZero="#false" symmetry="c" N="#1" cascade="#0" />
      <micro.DSP.PoleOrZero i="#0.5877632713709485" r="#0.8090329639930136" isPoint="#true" isPole="#false" isZero="#true" symmetry="c" N="#1" cascade="#3" />
      <micro.DSP.PoleOrZero i="#0.5877632713709485" r="#0.8090329639930136" isPoint="#true" isPole="#false" isZero="#true" symmetry="c" N="#1" cascade="#1" />
      <micro.DSP.PoleOrZero i="#0.5877632713709485" r="#0.8090329639930136" isPoint="#true" isPole="#false" isZero="#true" symmetry="c" N="#1" cascade="#0" />
      <micro.DSP.PoleOrZero i="#0.5877632713709485" r="#0.8090329639930136" isPoint="#true" isPole="#false" isZero="#true" symmetry="c" N="#1" cascade="#2" />
    </micro.DSP.PoleOrZeroContainer>
    <micro.DSP.GenericC.CodeGenerator generateTestCases="#false" />
    <micro.DSP.GainControl magnitude="#1" frequency="#0.3642578125" peak="#true" />
  </micro.DSP.IirButterworthFilter>
</micro.DSP.FilterDocument>

*/

static const int SBfilter_numStages = 4;
static const int SBfilter_coefficientLength = 20;
extern float SBfilter_coefficients[20];

typedef struct
{
	float state[16];
	float output;
} SBfilterType;

typedef struct
{
    float *pInput;
    float *pOutput;
    float *pState;
    float *pCoefficients;
    short count;
} SBfilter_executionState;


SBfilterType *SBfilter_create( void );
void SBfilter_destroy( SBfilterType *pObject );
void SBfilter_init( SBfilterType * pThis );
void SBfilter_reset( SBfilterType * pThis );
#define SBfilter_writeInput( pThis, input )  \
    SBfilter_filterBlock( pThis, &(input), &(pThis)->output, 1 );

#define SBfilter_readOutput( pThis )  \
    (pThis)->output

int SBfilter_filterBlock( SBfilterType * pThis, float * pInput, float * pOutput, unsigned int count );
#define SBfilter_outputToFloat( output )  \
    (output)

#define SBfilter_inputFromFloat( input )  \
    (input)

void SBfilter_filterBiquad( SBfilter_executionState * pExecState );
#endif // SBFILTER_H_

