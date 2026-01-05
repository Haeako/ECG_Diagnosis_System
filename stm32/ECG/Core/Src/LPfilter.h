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

// Begin header file, LPfilter.h

#ifndef LPFILTER_H_ // Include guards
#define LPFILTER_H_

/*
Generated code is based on the following filter design:
<micro.DSP.FilterDocument sampleFrequency="#500" arithmetic="float" biquads="Direct1" classname="LPfilter" inputMax="#1" inputShift="#15" >
  <micro.DSP.IirButterworthFilter N="#4" bandType="l" w1="#0.3" w2="#0.4999" stopbandRipple="#undefined" passbandRipple="#undefined" transitionRatio="#undefined" >
    <micro.DSP.FilterStructure coefficientBits="#0" variableBits="#0" accumulatorBits="#0" biquads="Direct1" >
      <micro.DSP.FilterSection form="Direct1" historyType="Double" accumulatorBits="#0" variableBits="#0" coefficientBits="#0" />
      <micro.DSP.FilterSection form="Direct1" historyType="Double" accumulatorBits="#0" variableBits="#0" coefficientBits="#0" />
    </micro.DSP.FilterStructure>
    <micro.DSP.PoleOrZeroContainer >
      <micro.DSP.PoleOrZero i="#0.6442020224775572" r="#-0.2265597603261922" isPoint="#true" isPole="#true" isZero="#false" symmetry="c" N="#1" cascade="#1" />
      <micro.DSP.PoleOrZero i="#0.19373023987241558" r="#-0.16448783868547645" isPoint="#true" isPole="#true" isZero="#false" symmetry="c" N="#1" cascade="#0" />
      <micro.DSP.PoleOrZero i="#0" r="#-1" isPoint="#true" isPole="#false" isZero="#true" symmetry="r" N="#1" cascade="#0" />
      <micro.DSP.PoleOrZero i="#0" r="#-1" isPoint="#true" isPole="#false" isZero="#true" symmetry="r" N="#1" cascade="#0" />
      <micro.DSP.PoleOrZero i="#0" r="#-1" isPoint="#true" isPole="#false" isZero="#true" symmetry="r" N="#1" cascade="#1" />
      <micro.DSP.PoleOrZero i="#0" r="#-1" isPoint="#true" isPole="#false" isZero="#true" symmetry="r" N="#1" cascade="#1" />
    </micro.DSP.PoleOrZeroContainer>
    <micro.DSP.GenericC.CodeGenerator generateTestCases="#false" />
    <micro.DSP.GainControl magnitude="#1" frequency="#0.0048828125" peak="#true" />
  </micro.DSP.IirButterworthFilter>
</micro.DSP.FilterDocument>

*/

static const int LPfilter_numStages = 2;
static const int LPfilter_coefficientLength = 10;
extern float LPfilter_coefficients[10];

typedef struct
{
	float state[8];
	float output;
} LPfilterType;

typedef struct
{
    float *pInput;
    float *pOutput;
    float *pState;
    float *pCoefficients;
    short count;
} LPfilter_executionState;


LPfilterType *LPfilter_create( void );
void LPfilter_destroy( LPfilterType *pObject );
void LPfilter_init( LPfilterType * pThis );
void LPfilter_reset( LPfilterType * pThis );
#define LPfilter_writeInput( pThis, input )  \
    LPfilter_filterBlock( pThis, &(input), &(pThis)->output, 1 );

#define LPfilter_readOutput( pThis )  \
    (pThis)->output

int LPfilter_filterBlock( LPfilterType * pThis, float * pInput, float * pOutput, unsigned int count );
#define LPfilter_outputToFloat( output )  \
    (output)

#define LPfilter_inputFromFloat( input )  \
    (input)

void LPfilter_filterBiquad( LPfilter_executionState * pExecState );
#endif // LPFILTER_H_

