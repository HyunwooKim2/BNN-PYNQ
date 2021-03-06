/******************************************************************************
 *  Copyright (c) 2016, Xilinx, Inc.
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are met:
 *
 *  1.  Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *
 *  2.  Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimer in the
 *      documentation and/or other materials provided with the distribution.
 *
 *  3.  Neither the name of the copyright holder nor the names of its
 *      contributors may be used to endorse or promote products derived from
 *      this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 *  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 *  THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 *  PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 *  CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 *  EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 *  PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 *  OR BUSINESS INTERRUPTION). HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 *  WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 *  OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 *  ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *****************************************************************************/
/******************************************************************************
 *
 *
 * @file top.cpp
 *
 * HLS Description of the CNV BNN with axi-lite based parameter loading (DoMemInit) 
 * and  dataflow architecture of the image inference (DoCompute).
 * The network uses 1 bit weights and 1 bit activation.
 *
 *****************************************************************************/
#include "config.h"

#include "bnn-library.h"

#include "weights.hpp"
#include "activations.hpp"
#include "interpret.hpp"
#include "mvau.hpp"

// hwkim modified for debug
//#define ACTIVATION_LOG
#ifdef ACTIVATION_LOG
#include <fstream>
int weighted_layer_cnt = 0;
int pooling_layer_cnt = 0;
#endif

static BinaryWeights<L0_SIMD, L0_PE, L0_WMEM>  weights0;
static BinaryWeights<L1_SIMD, L1_PE, L1_WMEM>  weights1;
static BinaryWeights<L2_SIMD, L2_PE, L2_WMEM>  weights2;
static BinaryWeights<L3_SIMD, L3_PE, L3_WMEM>  weights3;
static BinaryWeights<L4_SIMD, L4_PE, L4_WMEM>  weights4;
static BinaryWeights<L5_SIMD, L5_PE, L5_WMEM>  weights5;
static BinaryWeights<L6_SIMD, L6_PE, L6_WMEM>  weights6;
//static BinaryWeights<L7_SIMD, L7_PE, L7_WMEM>  weights7;
//static BinaryWeights<L8_SIMD, L8_PE, L8_WMEM>  weights8;

static ThresholdsActivation<L0_TMEM, L0_PE, L0_API, ap_fixed<24, 16>, ap_uint<L0_API> > threshs0;
static ThresholdsActivation<L1_TMEM, L1_PE, L1_API, ap_int<16>, ap_uint<L1_API>>  		threshs1;
static ThresholdsActivation<L2_TMEM, L2_PE, L2_API, ap_int<16>, ap_uint<L2_API>>  		threshs2;
static ThresholdsActivation<L3_TMEM, L3_PE, L3_API, ap_int<16>, ap_uint<L3_API>>  		threshs3;
static ThresholdsActivation<L4_TMEM, L4_PE, L4_API, ap_int<16>, ap_uint<L4_API>>  		threshs4;
static ThresholdsActivation<L5_TMEM, L5_PE, L5_API, ap_int<16>, ap_uint<L5_API>>  		threshs5;
// hwkim modified for last fc layer
//static ThresholdsActivation<L6_TMEM, L6_PE, L6_API, ap_int<16>, ap_uint<L6_API>>  		threshs6;
static PassThroughAndBatchNorm<L6_TMEM, L6_PE, L6_API, ap_int<16>, ap_int<16>>  		threshs6;

//static ThresholdsActivation<L7_TMEM, L7_PE, L7_API, ap_int<16>, ap_uint<L7_API>>  		threshs7;
/* hwkim commented
 * 8 layer는 없음
 * 마지막 layer라 thresholding(activation) 안 하고,
 * pass through activation
 */

// hwkim modified for average pooling
template <int AVE_IFM_CH, int AVE_IFM_DIM>
void average_pooling(
		stream<ap_uint<AVE_IFM_CH>> &in,
		stream<ap_uint<AVE_IFM_CH>> &out,
		int ave_thres)
{
	  unsigned int ave_sum[AVE_IFM_CH];
	  ap_uint<AVE_IFM_CH> ave_buf;
	  for(int ch=0; ch<AVE_IFM_CH; ch++){
	#pragma HLS UNROLL
		  ave_sum[ch] = 0;
	  }
	  for(int y=0; y<AVE_IFM_DIM; y++){
		for(int x=0; x<AVE_IFM_DIM; x++){
			ave_buf = in.read();
			for(int ch=0; ch<AVE_IFM_CH; ch++){
				ave_sum[ch] = ave_sum[ch] + ave_buf[ch];
			}
		}
	  }
	  ave_buf = 0;
	  for(int ch=0; ch<AVE_IFM_CH; ch++){
		  if(ave_sum[ch]>=ave_thres)
			  ave_buf[ch] = 1;
	  }
	  out.write(ave_buf);

}



// hwkim modified for padding
template <int IFM_DIM, int InWidth>
void insert_pad(stream<ap_uint<InWidth>> & in_stream,
		stream<ap_uint<InWidth>>& out_stream)
{
  for(int y=0; y<IFM_DIM; y++){
	  for(int x=0; x<IFM_DIM; x++){
		  if(x==0 || y==0 || x==(IFM_DIM-1) || y==(IFM_DIM-1)){
			  out_stream.write(0);
			  //inter1_pad.write((ap_uint<64>)0xFFFFFFFFFFFFFFFF);
		  }
		  else{
			  out_stream.write(in_stream.read());
		  }
//		  cout << setw(20) << hex << inter1_pad.read() << "|";
	  }
//	  cout << endl;
  }
//  cout << "padded stream size = " << out_stream.size() << endl;
}


// hwkim modified for debug
#ifdef ACTIVATION_LOG
string golden_file_dir = "/home/khw1204/work/params/guinness_params/cifar10_params/190621/Activations/";
template <int InWidth>
void activation_log(
		stream<ap_uint<InWidth>>& in_stream,
		int layer_cnt){

	unsigned char out_dim_config[11] = {32 ,32, 16, 16, 16, 8, 8, 8, 4, 1, 1};
	unsigned char layer_type[11] = {1, 2, 4, 2, 2, 4, 2, 2, 4, 8, 16};

	// file open
	string act_file_name;
	string golden_file_name;
	string compare_result_file_name;

	ofstream activation_log_file;
	ifstream golden_file;
	ofstream compare_result_file;

	act_file_name = "activation_" + to_string(layer_cnt+1) + "_log.txt";
	activation_log_file.open(act_file_name);
	if(!activation_log_file.is_open()){
		cout << act_file_name << " open error!!" << endl;
	}
	switch(layer_type[layer_cnt]){
	case 1:
	case 2:
		golden_file_name = golden_file_dir + "Sign" + to_string(weighted_layer_cnt+1) + ".txt";
		break;
	case 4:
		golden_file_name = golden_file_dir + "MaxPool" + to_string(pooling_layer_cnt+1) + ".txt";
		break;
	case 8:
	case 16:
		break;
	}
	golden_file.open(golden_file_name);
	if(!golden_file.is_open()){
		cout << golden_file_name << " open error!!" << endl;
	}

	compare_result_file_name = "compare_result_" + to_string(layer_cnt+1) + ".txt";
	compare_result_file.open(compare_result_file_name);
	if(!compare_result_file.is_open()){
	  cout << compare_result_file_name << " open error!" << endl;
	}

	// activation logging & compare result logging
	ap_uint<InWidth> act_buf;
	ap_uint<InWidth> gold_buf;
	//unsigned long long gold_buf;
//	cout << "inter" << (layer_cnt+1) << " stream size = " << in_stream.size() << endl;
	for(int y=0; y<out_dim_config[layer_cnt]; y++){
		  for(int x=0; x<out_dim_config[layer_cnt]; x++){
			  act_buf = in_stream.read();
			  //golden_file >> hex >> gold_buf;
			  golden_file >> hex >> gold_buf;
			  if(act_buf!=gold_buf){
				  compare_result_file << dec << "@(" << setw(2) << y << "," << setw(2) << x << ")" <<
						  hex << " golden: ";
				  if(InWidth>=64){
					  for(int i=0; i<InWidth/64; i++){
						  compare_result_file << (unsigned long long )(gold_buf >> 64*(InWidth/64-i-1));
					  }
					  compare_result_file << "," << endl << setw(17) << hex << "act: ";
					  for(int i=0; i<InWidth/64; i++){
						  compare_result_file << (unsigned long long )(act_buf >> 64*(InWidth/64-i-1));
					  }
					  compare_result_file << endl;
				  }
				  else{
					  compare_result_file << (unsigned long long )gold_buf << "," << endl
							  << setw(17) << hex << "act: "  << (unsigned long long )act_buf << endl;
				  }
			  }
			  if(InWidth>=64){
				  activation_log_file << hex;
				  for(int i=0; i<InWidth/64; i++){
					  activation_log_file << (unsigned long long )(act_buf >> 64*(InWidth/64-i-1));
				  }
				  activation_log_file << " | ";
			  }
			  else{
				  activation_log_file << hex << (unsigned long long )act_buf << " |\t";
			  }
		  }
		  activation_log_file << endl;
	}

	activation_log_file.close();
	golden_file.close();
	compare_result_file.close();

}
#endif

unsigned int paddedSizeHW(unsigned int in, unsigned int padTo) {
  if(in % padTo == 0) {
    return in;
  } else {
    return in + padTo - (in % padTo);
  }
}

void DoMemInit(unsigned int targetLayer, unsigned int targetMem, unsigned int targetInd,
		unsigned int targetThresh, ap_uint<64> val) {
  switch (targetLayer) {
    case 0:
      weights0.m_weights[targetMem][targetInd] = val;
      /* hwkim commented
       *
       * weights0
       * 	-> static BinaryWeights<L0_SIMD, L0_PE, L0_WMEM>  weights0;
       * 	-> m_weights를 멤버로 갖는 class
       *
       * m_weights
       * 	-> ap_uint<SIMD>  m_weights[PE][TILES];
       * 	-> SIMD - input channel number가 아님
       * 		>> input channel 중 SIMD 개수만큼 한 번에 한다는 말
       * 	-> PE - targetMem - output channel number가 아님
       * 		>> PE 별 - layer 0은 16개 PE - out ch가 64니까 PE 당 4번 씩 함
       * 		>> PE가 16개라는 말은 한 번에 output channel 16개씩 계산한다는 말
       * 	-> TILES - target ind(ex?) - 64-bit 단위 index
       * 		>> kernel의 다음 pixel 위치에 해당하는 x-channel ap_uint<x>
       * 		>> PE 별 weight 들
       * 			TILES == PE 당 가지는 weight 개수
       * 			PE 당 가지는 weight kernel 수는 out_ch/PE
       * 				layer0의 경우 64/16=4.
       * 				즉, 16개의 PE가 16개 output channel 동시에 계산,
       * 				즉, 각 PE는 output channel 4개 씩 수행
       * 					(4개의 weight kernel에 대해 conv 수행)
       * 				따라서 TILES == 36 = 3x3x4
       *
       * layer 0의 경우, SIMD가 3이기 때문에, weights0.m_weights는 ap_uint<3>인데 64-bit짜리 val을 넣음
       * 	-> weight file에서 64-bit에 3-bit만 저장되어 있음
       */
      break;
    case 1:
      threshs0.m_thresholds[targetMem][targetInd][targetThresh] = *reinterpret_cast<ap_fixed<64, 56> *>(&val);
      /* hwkim comment
       *
       * threshs0
       * 	-> static ThresholdsActivation< , L0_PE, L0_API, ap_fixed<24, 16>, ap_uint<L0_API> > threshs0;
       * 	-> m_thresholds를 멤버로 갖는 class
       * m_thresholds
       * 	-> TA m_thresholds[PE][NF][NumTH]; <-L0_API
       *                  L0_PE^   ^L0_TMEM(4)
       * 	-> TA (Threshold Accuracy?)
       * 		layer 0의 경우 ap_fixed<24, 16> - 24-bit fixed point (with sign), 16-bit int/8-bit fractal
       * 		다른 layer의 경우 ap_int<16>
       * 	-> NF - targetInd
       * 		L0_TMEM(4) - 64-bit line(output channel?) 단위
       * 		layer 0의 L0_TMEM(NF)가 4인 이유
       * 			각 PE 당, output channel 4개 씩 계산함
       * 			output channel 당 batch normalization 수행하므로 threshold가 1개 존재
       * 	-> PE - 걍 PE number - targetMem
       * 	-> NumTH - output channel 당 여러 개의 threshold가 있을 수 있는 듯
       * 		하지만 여기서는 1개만 있음
       * targetMem - PE number
       * targetInd - 64-bit line(pixel) index
       * 	layer 0의 경우 0~3으로 4개
       * targetThresh - 0 고정
       */
      break;
    case 2:
      weights1.m_weights[targetMem][targetInd] = val;
      break;
    case 3:
      threshs1.m_thresholds[targetMem][targetInd][targetThresh] = val;
      break;
    case 4:
      weights2.m_weights[targetMem][targetInd] = val;
      break;
    case 5:
      threshs2.m_thresholds[targetMem][targetInd][targetThresh] = val;
      break;
    case 6:
      weights3.m_weights[targetMem][targetInd] = val;
      break;
    case 7:
      threshs3.m_thresholds[targetMem][targetInd][targetThresh] = val;
      break;
    case 8:
      weights4.m_weights[targetMem][targetInd] = val;
      break;
    case 9:
      threshs4.m_thresholds[targetMem][targetInd][targetThresh] = val;
      break;
    case 10:
      weights5.m_weights[targetMem][targetInd] = val;
      break;
    case 11:
      threshs5.m_thresholds[targetMem][targetInd][targetThresh] = val;
      break;
    case 12:
      weights6.m_weights[targetMem][targetInd] = val;
      break;
    case 13:
      threshs6.m_thresholds[targetMem][targetInd][targetThresh] = val;
      break;
//    case 14:
//      weights7.m_weights[targetMem][targetInd] = val;
//      break;
//    case 15:
//      threshs7.m_thresholds[targetMem][targetInd][targetThresh] = val;
//      break;
//    case 16:
//      weights8.m_weights[targetMem][targetInd] = val;
//      break;
//    case 17:
//      // do nothing, no thres mem for layer 8 as PassThrough activation is used
//      break;
  }
}

void DoCompute(ap_uint<64> *in, ap_uint<64>* out, const unsigned int numReps) {
#pragma HLS DATAFLOW
  stream<ap_uint<64>> inter0("DoCompute.inter0");
  stream<ap_uint<192>> inter0_1("DoCompute.inter0_1");
  stream<ap_uint<24>> inter0_2("DoCompute.inter0_2");
#pragma HLS STREAM variable=inter0_2 depth=128
  /* hwkim commented
   *  단순 FIFO size 지정
   */
  stream<ap_uint<64>> inter1("DoCompute.inter1");
#pragma HLS STREAM variable=inter1 depth=128
  stream<ap_uint<64>> inter2("DoCompute.inter2");
  stream<ap_uint<64>> inter3("DoCompute.inter3");
#pragma HLS STREAM variable=inter3 depth=128
  stream<ap_uint<128>> inter4("DoCompute.inter4");
#pragma HLS STREAM variable=inter4 depth=128
  stream<ap_uint<128>> inter5("DoCompute.inter5");
  stream<ap_uint<128>> inter6("DoCompute.inter6");
#pragma HLS STREAM variable=inter6 depth=81
  stream<ap_uint<256>> inter7("DoCompute.inter7");
#pragma HLS STREAM variable=inter7 depth=1
  stream<ap_uint<256>> inter8("DoCompute.inter8");
#pragma HLS STREAM variable=inter8 depth=1

  // hwkim modified for 3rd max pool
//  stream<ap_uint<64>> inter9("DoCompute.inter9");
//#pragma HLS STREAM variable=inter9 depth=128
//  stream<ap_uint<64>> inter10("DoCompute.inter10");
//#pragma HLS STREAM variable=inter10 depth=3
  stream<ap_uint<256>> inter9("DoCompute.inter9");
#pragma HLS STREAM variable=inter9 depth=1
  stream<ap_uint<256>> inter10("DoCompute.inter10");
#pragma HLS STREAM variable=inter10 depth=1

  stream<ap_uint<64>> memOutStrm("DoCompute.memOutStrm");

  // hwkim modified for debug
#ifdef ACTIVATION_LOG
  stream<ap_uint<64>> inter1_log("DoCompute.inter1_log");
#pragma HLS STREAM variable=inter1_log depth=128
  stream<ap_uint<64>> inter2_log("DoCompute.inter2_log");
  stream<ap_uint<64>> inter3_log("DoCompute.inter3_log");
#pragma HLS STREAM variable=inter3_log depth=128
  stream<ap_uint<128>> inter4_log("DoCompute.inter4_log");
#pragma HLS STREAM variable=inter4_log depth=128
  stream<ap_uint<128>> inter5_log("DoCompute.inter5_log");
  stream<ap_uint<128>> inter6_log("DoCompute.inter6_log");
#pragma HLS STREAM variable=inter6_log depth=81
  stream<ap_uint<256>> inter7_log("DoCompute.inter7_log");
#pragma HLS STREAM variable=inter7_log depth=1
  stream<ap_uint<256>> inter8_log("DoCompute.inter8_log");
#pragma HLS STREAM variable=inter8_log depth=1
  stream<ap_uint<256>> inter9_log("DoCompute.inter9_log");
#pragma HLS STREAM variable=inter9_log depth=1
#endif

  // hwkim modified for padding
  //const unsigned int inBits = 32 * 32 * 3 * 8;
  const unsigned int inBits = 34 * 34 * 3 * 8;

  // commented by author
  // const unsigned int inBitsPadded = paddedSize(inBits, 64);

  // hwkim modified for padding
  //const unsigned int outBits = L8_MH*16;
  const unsigned int outBits = L6_MH*16;

  Mem2Stream_Batch<64, inBits / 8>(in, inter0, numReps);

  // hwkim modified for padding
  //StreamingDataWidthConverter_Batch<64, 192, (32 * 32 * 3 * 8) / 64>(inter0, inter0_1, numReps);
  StreamingDataWidthConverter_Batch<64, 192, (34*34*3*8)/64+1>(inter0, inter0_1, numReps);
  //StreamingDataWidthConverter_Batch<192, 24, (32 * 32 * 3 * 8) / 192>(inter0_1, inter0_2, numReps);
  StreamingDataWidthConverter_Batch<192, 24, (34*34*3*8)/192+1>(inter0_1, inter0_2, numReps);
  // convolutional layers
  ConvLayer_Batch<L0_K, L0_IFM_CH, L0_IFM_DIM, L0_OFM_CH, L0_OFM_DIM, L0_SIMD, L0_PE,
  	  	  Slice<ap_fixed<8, 1, AP_TRN, AP_SAT>>, Identity, Recast<Binary>>
	  (inter0_2, inter1,
// hwkim modified for debug
#ifdef ACTIVATION_LOG
			  inter1_log,
#endif
			  weights0, threshs0, numReps, ap_resource_lut());

  // hwkim modified for debug
#ifdef ACTIVATION_LOG
  activation_log(inter1_log, 0);
  weighted_layer_cnt++;
#endif

  // hwkim modified for padding
  stream<ap_uint<64>> inter1_pad("DoCompute.inter1_pad");
  insert_pad<L1_IFM_DIM,64>(inter1, inter1_pad);

  // hwkim modified for padding
//  ConvLayer_Batch<L1_K, L1_IFM_CH, L1_IFM_DIM, L1_OFM_CH, L1_OFM_DIM, L1_SIMD, L1_PE, Recast<XnorMul>>
//  	  (inter1, inter2, weights1, threshs1, numReps, ap_resource_lut());
  ConvLayer_Batch<L1_K, L1_IFM_CH, L1_IFM_DIM, L1_OFM_CH, L1_OFM_DIM, L1_SIMD, L1_PE, Recast<XnorMul>>
	  (inter1_pad, inter2,
// hwkim modified for debug
#ifdef ACTIVATION_LOG
			  inter2_log,
#endif
			  weights1, threshs1, numReps, ap_resource_lut());

  // hwkim modified for debug
#ifdef ACTIVATION_LOG
  activation_log(inter2_log,1);
  weighted_layer_cnt++;
#endif

  StreamingMaxPool_Batch<L1_OFM_DIM, 2, L1_OFM_CH>(inter2, inter3,
		  // hwkim modified for debug
#ifdef ACTIVATION_LOG
		  inter3_log,
#endif
		  numReps);

  // hwkim modified for debug
#ifdef ACTIVATION_LOG
  activation_log(inter3_log,2);
  pooling_layer_cnt++;
#endif


  // hwkim modified for padding
  stream<ap_uint<64>> inter3_pad("DoCompute.inter3_pad");
  insert_pad<L2_IFM_DIM, 64>(inter3, inter3_pad);

  // hwkim modified for padding
  //ConvLayer_Batch<L2_K, L2_IFM_CH, L2_IFM_DIM, L2_OFM_CH, L2_OFM_DIM, L2_SIMD, L2_PE, Recast<XnorMul>>(inter3,inter4,weights2, threshs2, numReps, ap_resource_lut());
  ConvLayer_Batch<L2_K, L2_IFM_CH, L2_IFM_DIM, L2_OFM_CH, L2_OFM_DIM, L2_SIMD, L2_PE, Recast<XnorMul>>
  (inter3_pad, inter4,
		  // hwkim modified for debug
  #ifdef ACTIVATION_LOG
			  inter4_log,
  #endif
			  weights2, threshs2, numReps, ap_resource_lut());

#ifdef ACTIVATION_LOG
  activation_log(inter4_log,3);
  weighted_layer_cnt++;
#endif

  // hwkim modified for padding
  stream<ap_uint<128>> inter4_pad("DoCompute.inter3_pad");
  insert_pad<L3_IFM_DIM, 128>(inter4, inter4_pad);

  // hwkim modified for padding
  //ConvLayer_Batch<L3_K, L3_IFM_CH, L3_IFM_DIM, L3_OFM_CH, L3_OFM_DIM, L3_SIMD, L3_PE, Recast<XnorMul>>(inter4, inter5,
  ConvLayer_Batch<L3_K, L3_IFM_CH, L3_IFM_DIM, L3_OFM_CH, L3_OFM_DIM, L3_SIMD, L3_PE, Recast<XnorMul>>(inter4_pad, inter5,
// hwkim modified for debug
#ifdef ACTIVATION_LOG
		  inter5_log,
#endif
		  weights3, threshs3, numReps, ap_resource_lut());

#ifdef ACTIVATION_LOG
  activation_log(inter5_log,4);
  weighted_layer_cnt++;
#endif

  StreamingMaxPool_Batch<L3_OFM_DIM, 2, L3_OFM_CH>(inter5, inter6,
		  // hwkim modified for debug
#ifdef ACTIVATION_LOG
		  inter6_log,
#endif
		  numReps);

  // hwkim modified for debug
#ifdef ACTIVATION_LOG
  activation_log(inter6_log,5);
  pooling_layer_cnt++;
#endif

  // hwkim modified for padding
  stream<ap_uint<128>> inter6_pad("DoCompute.inter3_pad");
  insert_pad<L4_IFM_DIM, 128>(inter6, inter6_pad);

  // hwkim modified for padding
  //ConvLayer_Batch<L4_K, L4_IFM_CH, L4_IFM_DIM, L4_OFM_CH, L4_OFM_DIM, L4_SIMD, L4_PE, Recast<XnorMul>>(inter6, inter7,
  ConvLayer_Batch<L4_K, L4_IFM_CH, L4_IFM_DIM, L4_OFM_CH, L4_OFM_DIM, L4_SIMD, L4_PE, Recast<XnorMul>>(inter6_pad, inter7,
// hwkim modified for debug
#ifdef ACTIVATION_LOG
		  inter7_log,
#endif
		  weights4, threshs4, numReps, ap_resource_lut());

#ifdef ACTIVATION_LOG
  activation_log(inter7_log,6);
  weighted_layer_cnt++;
#endif

  // hwkim modified for padding
  stream<ap_uint<256>> inter7_pad("DoCompute.inter3_pad");
  insert_pad<L5_IFM_DIM, 256>(inter7, inter7_pad);

  // hwkim modified for padding
  //ConvLayer_Batch<L5_K, L5_IFM_CH, L5_IFM_DIM, L5_OFM_CH, L5_OFM_DIM, L5_SIMD, L5_PE,Recast<XnorMul>>(inter7, inter8,
  ConvLayer_Batch<L5_K, L5_IFM_CH, L5_IFM_DIM, L5_OFM_CH, L5_OFM_DIM, L5_SIMD, L5_PE,Recast<XnorMul>>(inter7_pad, inter8,
// hwkim modified for debug
#ifdef ACTIVATION_LOG
			  inter8_log,
#endif
  			  weights5, threshs5, numReps, ap_resource_lut());

#ifdef ACTIVATION_LOG
  activation_log(inter8_log,7);
  weighted_layer_cnt++;
#endif

  // hwkim modified for 3rd max pool layer
  StreamingMaxPool_Batch<L5_OFM_DIM, 2, L5_OFM_CH>(inter8, inter9,
		  // hwkim modified for debug
#ifdef ACTIVATION_LOG
		  inter9_log,
#endif
		  numReps);

  // hwkim modified for debug
#ifdef ACTIVATION_LOG
  activation_log(inter9_log,8);
  pooling_layer_cnt++;
#endif

  // hwkim modified for average pool
#define AVE_IFM_CH 256
#define AVE_OFM_CH 256
#define AVE_IFM_DIM 4
#define AVE_THRES (4*4/2)
  average_pooling<AVE_IFM_CH, AVE_IFM_DIM>(inter9,inter10,AVE_THRES);

//  unsigned int ave_sum[AVE_IFM_CH];
//  ap_uint<AVE_IFM_CH> ave_buf;
//  for(int ch=0; ch<AVE_IFM_CH; ch++){
//#pragma HLS UNROLL
//	  ave_sum[ch] = 0;
//  }
//  for(int y=0; y<AVE_IFM_DIM; y++){
//	for(int x=0; x<AVE_IFM_DIM; x++){
//		ave_buf = inter9.read();
//		for(int ch=0; ch<AVE_IFM_CH; ch++){
//			ave_sum[ch] = ave_sum[ch] + ave_buf[ch];
//		}
//	}
//  }
//  ave_buf = 0;
//  for(int ch=0; ch<AVE_IFM_CH; ch++){
//	  if(ave_sum[ch]>=AVE_THRES)
//		  ave_buf[ch] = 1;
//  }
//  inter10.write(ave_buf);

//  StreamingFCLayer_Batch<L6_MW, L6_MH, L6_SIMD, L6_PE, Recast<XnorMul>>
//    (inter8, inter9,  weights6, threshs6, numReps, ap_resource_lut());
//  StreamingFCLayer_Batch<L7_MW, L7_MH, L7_SIMD, L7_PE, Recast<XnorMul>>
//    (inter9, inter10, weights7, threshs7, numReps, ap_resource_lut());
//  StreamingFCLayer_Batch<L8_MW, L8_MH, L8_SIMD, L8_PE,
//  	  Recast<XnorMul>, Slice<ap_uint<16> >>
//    (inter10, static_cast<hls::stream<ap_uint<16 * L8_PE>>&>(wa_out),weights8, PassThroughActivation<ap_uint<16>>(), numReps, ap_resource_lut());

  // fully connected layers
  {
//  WidthAdjustedOutputStream<16 * L8_PE, 64, L8_MH / L8_PE>  wa_out(memOutStrm, numReps);
  WidthAdjustedOutputStream<16 * L6_PE, 64, L6_MH / L6_PE>  wa_out(memOutStrm, numReps);

//  StreamingFCLayer_Batch<L6_MW, L6_MH, L6_SIMD, L6_PE, Recast<XnorMul>, Slice<ap_uint<16> >>(inter8,static_cast<hls::stream<ap_uint<16 * L6_PE>>&>(wa_out),weights6,PassThroughActivation<ap_uint<16>>(),numReps,ap_resource_lut());
  StreamingFCLayer_Batch<L6_MW, L6_MH, L6_SIMD, L6_PE, Recast<XnorMul>, Slice<ap_uint<16> >>
  	  (inter10,
  	  static_cast<hls::stream<ap_uint<16 * L6_PE>>&>(wa_out),
  	  weights6,
  	  //PassThroughActivation<ap_uint<16>>(),
	  threshs6,
  	  numReps,
  	  ap_resource_lut());
  }
  Stream2Mem_Batch<64, outBits/8>(memOutStrm, out, numReps);
}

void BlackBoxJam(ap_uint<64> *in, ap_uint<64> *out, bool doInit,
		unsigned int targetLayer, unsigned int targetMem,
		unsigned int targetInd, unsigned int targetThresh, ap_uint<64> val, unsigned int numReps) {
	/* hwkim commented
	 * numReps - number of repetitions? (input image 장 수)
	 */
// pragmas for MLBP jam interface
// signals to be mapped to the AXI Lite slave port
#pragma HLS INTERFACE s_axilite port=return bundle=control
#pragma HLS INTERFACE s_axilite port=doInit bundle=control
#pragma HLS INTERFACE s_axilite port=targetLayer bundle=control
#pragma HLS INTERFACE s_axilite port=targetMem bundle=control
#pragma HLS INTERFACE s_axilite port=targetInd bundle=control
#pragma HLS INTERFACE s_axilite port=targetThresh bundle=control
#pragma HLS INTERFACE s_axilite port=val bundle=control
#pragma HLS INTERFACE s_axilite port=numReps bundle=control
// signals to be mapped to the AXI master port (hostmem)
#pragma HLS INTERFACE m_axi offset=slave port=in bundle=hostmem depth=512
#pragma HLS INTERFACE s_axilite port=in bundle=control
#pragma HLS INTERFACE m_axi offset=slave port=out bundle=hostmem depth=16
#pragma HLS INTERFACE s_axilite port=out bundle=control

// partition PE arrays
#pragma HLS ARRAY_PARTITION variable=weights0.m_weights complete dim=1
#pragma HLS ARRAY_PARTITION variable=threshs0.m_thresholds complete dim=1
#pragma HLS ARRAY_PARTITION variable=threshs0.m_thresholds complete dim=3
#pragma HLS ARRAY_PARTITION variable=weights1.m_weights complete dim=1
#pragma HLS ARRAY_PARTITION variable=threshs1.m_thresholds complete dim=1
#pragma HLS ARRAY_PARTITION variable=threshs1.m_thresholds complete dim=3
#pragma HLS ARRAY_PARTITION variable=weights2.m_weights complete dim=1
#pragma HLS ARRAY_PARTITION variable=threshs2.m_thresholds complete dim=1
#pragma HLS ARRAY_PARTITION variable=threshs2.m_thresholds complete dim=3
#pragma HLS ARRAY_PARTITION variable=weights3.m_weights complete dim=1
#pragma HLS ARRAY_PARTITION variable=threshs3.m_thresholds complete dim=1
#pragma HLS ARRAY_PARTITION variable=threshs3.m_thresholds complete dim=3
#pragma HLS ARRAY_PARTITION variable=weights4.m_weights complete dim=1
#pragma HLS ARRAY_PARTITION variable=threshs4.m_thresholds complete dim=1
#pragma HLS ARRAY_PARTITION variable=threshs4.m_thresholds complete dim=3
#pragma HLS ARRAY_PARTITION variable=weights5.m_weights complete dim=1
#pragma HLS ARRAY_PARTITION variable=threshs5.m_thresholds complete dim=1
#pragma HLS ARRAY_PARTITION variable=threshs5.m_thresholds complete dim=3
#pragma HLS ARRAY_PARTITION variable=weights6.m_weights complete dim=1
#pragma HLS ARRAY_PARTITION variable=threshs6.m_thresholds complete dim=1
#pragma HLS ARRAY_PARTITION variable=threshs6.m_thresholds complete dim=3
//#pragma HLS ARRAY_PARTITION variable=weights7.m_weights complete dim=1
//#pragma HLS ARRAY_PARTITION variable=threshs7.m_thresholds complete dim=1
//#pragma HLS ARRAY_PARTITION variable=threshs7.m_thresholds complete dim=3
//#pragma HLS ARRAY_PARTITION variable=weights8.m_weights complete dim=1

  if (doInit) {
    DoMemInit(targetLayer, targetMem, targetInd, targetThresh, val);
  } else {
	  // hwkim modified for analysis - undefined latency
//    DoCompute(in, out, numReps);
	  DoCompute(in, out, 1);
  }
}
