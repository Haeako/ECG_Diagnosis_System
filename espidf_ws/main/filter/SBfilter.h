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

// Begin header file, HPfilter.h

#ifndef HPFILTER_H_ // Include guards
#define HPFILTER_H_

/*
Generated code is based on the following filter design:
<micro.DSP.FilterDocument sampleFrequency="#360" arithmetic="float" biquads="Direct1" classname="HPfilter" inputMax="#1" inputShift="#-1" >
  <micro.DSP.IirButterworthFilter N="#4" bandType="s" w1="#0.1375" w2="#0.14027777777777778" stopbandRipple="#undefined" passbandRipple="#undefined" transitionRatio="#undefined" >
    <micro.DSP.FilterStructure coefficientBits="#0" variableBits="#0" accumulatorBits="#0" biquads="Direct1" >
      <micro.DSP.FilterSection form="Direct1" historyType="Double" accumulatorBits="#0" variableBits="#0" coefficientBits="#0" />
      <micro.DSP.FilterSection form="Direct1" historyType="Double" accumulatorBits="#0" variableBits="#0" coefficientBits="#0" />
      <micro.DSP.FilterSection form="Direct1" historyType="Double" accumulatorBits="#0" variableBits="#0" coefficientBits="#0" />
      <micro.DSP.FilterSection form="Direct1" historyType="Double" accumulatorBits="#0" variableBits="#0" coefficientBits="#0" />
    </micro.DSP.FilterStructure>
    <micro.DSP.PoleOrZeroContainer >
      <micro.DSP.PoleOrZero i="#0.758311693855142" r="#0.6468011043786357" isPoint="#true" isPole="#true" isZero="#false" symmetry="c" N="#1" cascade="#3" />
      <micro.DSP.PoleOrZero i="#0.768607581585411" r="#0.6344611174288208" isPoint="#true" isPole="#true" isZero="#false" symmetry="c" N="#1" cascade="#2" />
      <micro.DSP.PoleOrZero i="#0.7577413249642091" r="#0.6402160224834392" isPoint="#true" isPole="#true" isZero="#false" symmetry="c" N="#1" cascade="#1" />
      <micro.DSP.PoleOrZero i="#0.761966379677085" r="#0.6351116152731777" isPoint="#true" isPole="#true" isZero="#false" symmetry="c" N="#1" cascade="#0" />
      <micro.DSP.PoleOrZero i="#0.7660239043861035" r="#0.642812086001088" isPoint="#true" isPole="#false" isZero="#true" symmetry="c" N="#1" cascade="#1" />
      <micro.DSP.PoleOrZero i="#0.7660239043861035" r="#0.642812086001088" isPoint="#true" isPole="#false" isZero="#true" symmetry="c" N="#1" cascade="#3" />
      <micro.DSP.PoleOrZero i="#0.7660239043861035" r="#0.642812086001088" isPoint="#true" isPole="#false" isZero="#true" symmetry="c" N="#1" cascade="#0" />
      <micro.DSP.PoleOrZero i="#0.7660239043861035" r="#0.642812086001088" isPoint="#true" isPole="#false" isZero="#true" symmetry="c" N="#1" cascade="#2" />
    </micro.DSP.PoleOrZeroContainer>
    <micro.DSP.GenericC.CodeGenerator generateTestCases="#false" />
    <micro.DSP.GainControl magnitude="#1" frequency="#0.4189453125" peak="#true" />
  </micro.DSP.IirButterworthFilter>
</micro.DSP.FilterDocument>

*/

static const int HPfilter_numStages = 4;
static const int HPfilter_coefficientLength = 20;
extern float HPfilter_coefficients[20];

typedef struct
{
	float state[16];
	float output;
} HPfilterType;

typedef struct
{
    float *pInput;
    float *pOutput;
    float *pState;
    float *pCoefficients;
    short count;
} HPfilter_executionState;


HPfilterType *HPfilter_create( void );
void HPfilter_destroy( HPfilterType *pObject );
void HPfilter_init( HPfilterType * pThis );
void HPfilter_reset( HPfilterType * pThis );
#define HPfilter_writeInput( pThis, input )  \
    HPfilter_filterBlock( pThis, &(input), &(pThis)->output, 1 );

#define HPfilter_readOutput( pThis )  \
    (pThis)->output

int HPfilter_filterBlock( HPfilterType * pThis, float * pInput, float * pOutput, unsigned int count );
#define HPfilter_outputToFloat( output )  \
    (output)

#define HPfilter_inputFromFloat( input )  \
    (input)

void HPfilter_filterBiquad( HPfilter_executionState * pExecState );
#endif // HPFILTER_H_
	
