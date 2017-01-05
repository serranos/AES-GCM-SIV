/*
###############################################################################
# AES-GCM-SIV developers and authors:                                         #
#                                                                             #
# Shay Gueron,    University of Haifa, Israel and                             #
#                 Intel Corporation, Israel Development Center, Haifa, Israel #
# Adam Langley,   Google                                                      #
# Yehuda Lindell, Bar Ilan University                                         #
###############################################################################
#                                                                             #
# References:                                                                 #
#                                                                             #
# [1] S. Gueron, Y. Lindell, GCM-SIV: Full Nonce Misuse-Resistant             #
# Authenticated Encryption at Under One Cycle per Byte,                       #
# 22nd ACM Conference on Computer and Communications Security,                #
# 22nd ACM CCS: pages 109-119, 2015.                                          #
# [2] S. Gueron, A. Langley, Y. Lindell, AES-GCM-SIV: Nonce Misuse-Resistant  #
# Authenticated Encryption.                                                   #
# https://tools.ietf.org/html/draft-gueron-gcmsiv-02#                         #
###############################################################################
#                                                                             #
###############################################################################
#                                                                             #
# Copyright (c) 2016, Shay Gueron                                             #
#                                                                             #
#                                                                             #
# Permission to use this code for AES-GCM-SIV is granted.                     #
#                                                                             #
# Redistribution and use in source and binary forms, with or without          #
# modification, are permitted provided that the following conditions are      #
# met:                                                                        #
#                                                                             #
# * Redistributions of source code must retain the above copyright notice,    #
#   this list of conditions and the following disclaimer.                     #
#                                                                             #
# * Redistributions in binary form must reproduce the above copyright         #
#   notice, this list of conditions and the following disclaimer in the       #
#   documentation and/or other materials provided with the distribution.      #
#                                                                             #
# * The names of the contributors may not be used to endorse or promote       #
# products derived from this software without specific prior written          #
# permission.                                                                 #
#                                                                             #
###############################################################################
#                                                                             #
###############################################################################
# THIS SOFTWARE IS PROVIDED BY THE AUTHORS ""AS IS"" AND ANY                  #
# EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE           #
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR          #
# PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL INTEL CORPORATION OR              #
# CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,       #
# EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,         #
# PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR          #
# PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF      #
# LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING        #
# NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS          #
# SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.                #
###############################################################################
*/
#include <stdint.h>
#include <stdio.h>
#include <wmmintrin.h>
#include <string.h>
#include "measurements.h"
#include "GCM_SIV.h"


#define LENGTH (16)
#define XOR_WITH_NONCE

void Clear_SIV_CTX(AES_GCM_SIV_CONTEXT* ctx)
{
	int i;
	for (i=0;i<16*15;i++)
		ctx->KS.KEY[i] = 0;
	ctx->KS.nr = 0;
	for (i=0;i<16*8;i++)
		ctx->Htbl[i] = 0;
	for (i=0;i<16*16;i++)
		ctx->secureBuffer[i] = 0;
}

void AES_GCM_SIV_Encrypt (AES_GCM_SIV_CONTEXT* ctx, uint8_t* CT, uint8_t* TAG, const uint8_t* AAD, const uint8_t* PT, size_t L1, size_t L2, const uint8_t* IV, const uint8_t* KEY)
{
	uint64_t len_blk[2];
	uint8_t Record_Hash_Key[16] = {0};
	uint8_t Record_Enc_Key[32] = {0};
	uint8_t T[16] = {0};
	uint64_t AND_MASK[2] = {0xffffffffffffffff,0x7fffffffffffffff};
	uint8_t *T1, *T2, *T3;
	T1 = Record_Hash_Key;
	T2 = &Record_Enc_Key[16];
	T3 = Record_Enc_Key;
	len_blk[0] = (uint64_t)L1*8;
	len_blk[1] = (uint64_t)L2*8;
    if ((L1+L2) <= 128) { //HORNER
		AES256_KS_ENC_x1(IV, T1 , (unsigned char *)(ctx->KS.KEY), KEY);    		//T1 = AES_K (IV) - T1 = Record_Hash_Key
		ECB_ENC_block(T1, T2 , (unsigned char *)(ctx->KS.KEY));    		//T2 = AES_K (T1) - T2 = Record_Enc_Key[255:128]
		ECB_ENC_block(T2, T3 , (unsigned char *)(ctx->KS.KEY));    		//T3 = AES_K (T2) - T3 = Record_Enc_Key[127:0]
		Polyval_Horner(T, Record_Hash_Key, AAD, L1);					// using non padded inputs as needed
		Polyval_Horner(T, Record_Hash_Key, PT, L2);
		Polyval_Horner(T, Record_Hash_Key, len_blk, 16);
		#ifdef DETAILS
		memcpy((uint8_t*)(ctx->details_info+16*16), T, 16);
		#endif
		#ifdef XOR_WITH_NONCE
        *(__m128i*)T = _mm_xor_si128(*((__m128i*)T), *((__m128i*)IV));
		#endif
		#ifdef DETAILS
		memcpy((uint8_t*)(ctx->details_info+16*17), T, 16);
		#endif
		*((__m128i*)T) = _mm_and_si128(*((__m128i*)AND_MASK), *((__m128i*)T));	//TxorIV_masked = [0]TxorIV[127..0]  (MS bit is cleared)
		#ifdef DETAILS
		memcpy((uint8_t*)(ctx->details_info+16*18), T, 16);
		#endif
		AES256_KS_ENC_x1(T, TAG,(unsigned char *)(ctx->KS.KEY), Record_Enc_Key);                     //TAG = AES_Record_Enc_Key (TxorIV_masked)
		ENC_MSG_x4(PT, CT, TAG, (unsigned char *)(ctx->KS.KEY), L2);			//CT = AES_Record_Enc_Key(CTRBLCK) xor MSG		
    }
    else 
	{ //Htable    
	    AES256_KS_ENC_x1(IV, T1 , (unsigned char *)(ctx->KS.KEY), KEY);    		//T1 = AES_K (IV) - T1 = Record_Hash_Key
		ECB_ENC_block(T1, T2 , (unsigned char *)(ctx->KS.KEY));    		//T2 = AES_K (T1) - T2 = Record_Enc_Key[255:128]
		ECB_ENC_block(T2, T3 , (unsigned char *)(ctx->KS.KEY));    		//T3 = AES_K (T2) - T3 = Record_Enc_Key[127:0]	
		INIT_Htable(ctx->Htbl, Record_Hash_Key);								//T = POLYVAL(padded_AADAAD||padded_MSG||LENBLK)
		Polyval_Htable(ctx->Htbl, AAD, L1, T);
		Polyval_Htable(ctx->Htbl, PT, L2, T);
		Polyval_Htable(ctx->Htbl, len_blk, 16, T);
		#ifdef DETAILS
		memcpy((uint8_t*)(ctx->details_info+16*16), T, 16);
		#endif
		#ifdef XOR_WITH_NONCE
        *(__m128i*)T = _mm_xor_si128(*((__m128i*)T), *((__m128i*)IV));
		#endif		
		#ifdef DETAILS
		memcpy((uint8_t*)(ctx->details_info+16*17), T, 16);
		#endif
		*((__m128i*)T) = _mm_and_si128(*((__m128i*)AND_MASK), *((__m128i*)T));	//TxorIV_masked = [0]TxorIV[120..0]  (MS bit is cleared)
		#ifdef DETAILS
		memcpy((uint8_t*)(ctx->details_info+16*18), T, 16);
		#endif
		AES256_KS_ENC_x1(T, TAG, (unsigned char *)(ctx->KS.KEY), Record_Enc_Key);                                //TAG = AES_ENC_KEY (TxorIV_masked)
		ENC_MSG_x8(PT, CT, TAG, (unsigned char *)(ctx->KS.KEY), L2);			//CT = AES_K (CTRBLCK) xor MSG		
	}
	
	#ifdef DETAILS
	memcpy((unsigned char *)ctx->details_info, (unsigned char *)(ctx->KS.KEY), 16*15);
	
	memcpy(ctx->details_info+16*19, TAG, 16);
	memcpy(ctx->details_info+16*20, Record_Hash_Key, 16);
	memcpy(ctx->details_info+16*21, Record_Enc_Key, 32);
	memcpy(ctx->details_info+16*46, ctx->Htbl, 16*8);
	#endif
}

/*
AES GCM SIV Decrypt - returns 0 if true , 1 if false.
*/

int AES_GCM_SIV_Decrypt(AES_GCM_SIV_CONTEXT* ctx, uint8_t* DT, uint8_t* TAG, const uint8_t* AAD, const uint8_t* CT, size_t L1, size_t L2, const uint8_t* IV, const uint8_t* KEY)
{
	uint64_t len_blk[2];
	uint8_t Record_Hash_Key[16] = {0};
	uint8_t Record_Enc_Key[32] = {0};
	uint8_t TAG_dec[16] = {0};
	uint64_t AND_MASK[2] = {0xffffffffffffffff,0x7fffffffffffffff};
	uint8_t *T1, *T2, *T3;
	uint8_t POLYVAL_dec[16]={0};
	int i;
	T1 = Record_Hash_Key;
	T2 = &Record_Enc_Key[16];
	T3 = Record_Enc_Key;
	len_blk[0] = (uint64_t)L1*8;
	len_blk[1] = (uint64_t)L2*8;

	AES256_KS_ENC_x1(IV, T1 , (unsigned char *)(ctx->KS.KEY), KEY);    		//T1 = AES_K (IV) - T1 = Record_Hash_Key
	ECB_ENC_block(T1, T2 , (unsigned char *)(ctx->KS.KEY));    		//T2 = AES_K (T1) - T2 = Record_Enc_Key[255:128]
	ECB_ENC_block(T2, T3 , (unsigned char *)(ctx->KS.KEY));    		//T3 = AES_K (T2) - T3 = Record_Enc_Key[127:0]
	INIT_Htable_6(ctx->Htbl, Record_Hash_Key);
	AES_256_KS(Record_Enc_Key, (unsigned char *)(ctx->KS.KEY));
	
	
	Polyval_Horner(POLYVAL_dec, Record_Hash_Key, AAD, L1);													//POLYVAL(padded_AAD)
	Decrypt_Htable(CT, DT, POLYVAL_dec, TAG, ctx->Htbl, (unsigned char *)(ctx->KS.KEY), L2, ctx->secureBuffer);
	Polyval_Horner(POLYVAL_dec, Record_Hash_Key, len_blk, 16);														//POLYVAL(padded_AAD||padded_MSG||LENBLK)
	//Calculate TAG_dec
	#ifdef DETAILS
	memcpy((uint8_t*)(ctx->details_info+16*39), POLYVAL_dec, 16);
	#endif
	#ifdef XOR_WITH_NONCE
    *(__m128i*)POLYVAL_dec = _mm_xor_si128(*((__m128i*)POLYVAL_dec), *((__m128i*)IV));
	#endif	
	#ifdef DETAILS
	memcpy((uint8_t*)(ctx->details_info+16*40), POLYVAL_dec, 16);
	#endif
	*((__m128i*)POLYVAL_dec) = _mm_and_si128(*((__m128i*)AND_MASK), *((__m128i*)POLYVAL_dec));		//MSbit cleared
	#ifdef DETAILS
	memcpy((uint8_t*)(ctx->details_info+16*41), POLYVAL_dec, 16);
	#endif
	ECB_ENC_block(POLYVAL_dec, TAG_dec, (unsigned char *)(ctx->KS.KEY));									//TAG_dec = AES_K (POLYVAL_masked)

	
	#ifdef DETAILS
	memcpy((unsigned char *)(ctx->details_info+16*23), (unsigned char *)(ctx->KS.KEY), 16*15);
	memcpy(ctx->details_info+16*42, TAG_dec, 16);
	memcpy(ctx->details_info+16*43, Record_Hash_Key, 16);
	memcpy(ctx->details_info+16*44, Record_Enc_Key, 32);
	#endif

	if (memcmp(TAG, TAG_dec, 16) != 0)
    {
        for (i=0; i<L2; i++)
        {
            DT[i] = CT[i];
        }
		return 1;
    }
	return 0;
}