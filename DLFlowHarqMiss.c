#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* =======================================================================================================*/
/*                                          Variables                                                     */
/* =======================================================================================================*/
/* Slot pattern 1 : 7D 1S 2U */
/* Slot pattern 2 : 3D 1S 2U 4D */
#define SLOT_PATTERN 1
#define NUM_DL_HARQ 16
#define MIN_K1 4 

#define DU_DL_PROCESSDELAY 2
#define NFAPI_DL_TRANSPORT_DELAY 1
#define RU_DL_PROCESSDELAY 3

#define RU_UL_PROCESSDELAY 1
#define NFAPI_UL_TRANSPORT_DELAY 1
#define DU_UL_PROCESSDELAY 1

/* =======================================================================================================*/
/*                                          Macros                                                        */
/* =======================================================================================================*/
#define UL_SLOT 0
#define DL_SLOT 1
#define SPECIAL_SLOT 2

#define MAX_SLOTS 64
#define MAX_SFN 1024
#define TOTAL_DL_PROCESSING_DELAY (DU_DL_PROCESSDELAY + NFAPI_DL_TRANSPORT_DELAY + RU_DL_PROCESSDELAY)
#define TOTAL_UL_PROCESSING_DELAY (RU_UL_PROCESSDELAY + NFAPI_UL_TRANSPORT_DELAY + DU_UL_PROCESSDELAY)

#define FATAL_PRINT printf("[FATAL] %s:%d \n", __FUNCTION__, __LINE__);
#define ENABLE_STATS
#define ENBALE_LOGS

uint8_t pattern[20];
typedef struct _sfnSlotInfo
{
   uint16_t sfn;
   uint16_t slot;
}sfnSlotInfo;
sfnSlotInfo gCurSlotSfn;

typedef enum _harqState
{
   HARQ_FREE = 0,
   HARQ_WAIT_FOR_FEEDBACK = 1,
   HARQ_ACKED = 2
}harqState;

typedef struct _slotHarqInfo
{
   harqState hqState;
   int8_t harqID;
}slotHarqInfo;

typedef struct _slotInfo
{
   uint8_t numHARQ;
   slotHarqInfo slotHarqInf[NUM_DL_HARQ];
}slotInfo;

typedef struct _nonPersistentStats
{
   uint64_t harqFailCount[20];
   uint64_t harqSuccessCount[20];
}nonPersistentStats;

uint8_t harqInfo[NUM_DL_HARQ];
slotInfo slotInf[MAX_SLOTS];
nonPersistentStats slotStats;

/* =======================================================================================================*/
/*                                          Utility API                                                   */
/* =======================================================================================================*/
void incSfnSlot(sfnSlotInfo *pSrcSfnSlot, uint8_t incBy, sfnSlotInfo *pDstSfnSlot)
{
   memset(pDstSfnSlot, 0, sizeof(sfnSlotInfo));
   if ((pSrcSfnSlot->slot + (incBy)) >= 20)
   {
      pDstSfnSlot->sfn = (pSrcSfnSlot->sfn + (pSrcSfnSlot->slot + (incBy)) / 20);
   }
   else
   {
      pDstSfnSlot->sfn = pSrcSfnSlot->sfn;
   }
   pDstSfnSlot->slot = (pSrcSfnSlot->slot + (incBy)) % 20;
   if (pDstSfnSlot->sfn >= MAX_SFN)
   {
      pDstSfnSlot->sfn = pDstSfnSlot->sfn % MAX_SFN;
   }  
}

void decSfnSlot(sfnSlotInfo *pSrcSfnSlot, uint8_t decBy, sfnSlotInfo *pDstSfnSlot)
{
   memset(pDstSfnSlot, 0, sizeof(sfnSlotInfo));
   int32_t  slot;
   slot = pSrcSfnSlot->sfn * 20 + pSrcSfnSlot->slot; 
   slot = slot - decBy;
   if(slot < 0)
   {
      slot = (MAX_SFN * MAX_SFN * 20) + slot;
   }
   slot = slot % (MAX_SFN * 20);
   pDstSfnSlot->sfn = slot / 20;
   pDstSfnSlot->slot = slot % 20;
}

void incGlobalSfnSlot(uint8_t incBy)
{

   sfnSlotInfo dstSfnSlot;
   incSfnSlot(&gCurSlotSfn, incBy, &dstSfnSlot);

   memcpy(&gCurSlotSfn, &dstSfnSlot, sizeof(sfnSlotInfo));
}

uint8_t getSlotDbIndex(sfnSlotInfo *pSlotInfo)
{
   uint8_t index = ((pSlotInfo->sfn * 20) + pSlotInfo->slot) % MAX_SLOTS;

   return index;
}

uint8_t getAvailK1(sfnSlotInfo *pSfnSlot)
{
   uint8_t index = MIN_K1;
   /* get Next UL/Special slot */
   
   do
   {
      sfnSlotInfo dstSfnSlot = {0};
   
      incSfnSlot(pSfnSlot, index, &dstSfnSlot);
      if ((pattern[dstSfnSlot.slot] == SPECIAL_SLOT) || (pattern[dstSfnSlot.slot] == UL_SLOT))
      {
         return index;
      }
      index++;
   } while (1);
   
}

int8_t getAvailHarQID()
{
   for(int i = 0; i < NUM_DL_HARQ; i++)
   {
      if (harqInfo[i] == HARQ_FREE)
      {
         return i;
      }
   }
   return -1;
}

/* =======================================================================================================*/
/*                                          Bootup API                                                    */
/* =======================================================================================================*/
void initPattern(uint8_t patternConfig)
{
   if (patternConfig == 1)
   {
      for(int i = 0; i< 20; i++)
      {
         if (i == 8 || i == 9 || i == 18 || i == 19)
            pattern[i] = UL_SLOT;
         else if (i == 7 || i == 17)
            pattern[i] = SPECIAL_SLOT;
         else
            pattern[i] = DL_SLOT;
      }
   }
   else if (patternConfig == 2)
   {
      for(int i = 0; i< 20; i++)
      {
         if (i == 4 || i == 5 || i == 14 || i == 15)
            pattern[i] = UL_SLOT;
         else if (i == 3 || i == 13)
            pattern[i] = SPECIAL_SLOT;
         else
            pattern[i] = DL_SLOT;
      }
   }
}

void initSystem(void)
{
   initPattern(SLOT_PATTERN);

   gCurSlotSfn.sfn = 0;
   gCurSlotSfn.slot = 0;

   memset(slotInf, 0, (sizeof(slotInfo) * MAX_SLOTS));

   for(int i = 0; i < NUM_DL_HARQ; i++)
   {
      harqInfo[i] = HARQ_FREE;
   }
   memset(&slotStats, 0, sizeof(nonPersistentStats));
}

/* =======================================================================================================*/
/*                                          Database API                                                  */
/* =======================================================================================================*/

void fillSlotDb(sfnSlotInfo *pSlotInfo, int8_t harqID)
{
   uint8_t index = getSlotDbIndex(pSlotInfo);

   uint8_t harqIdx = slotInf[index].numHARQ;
   slotInf[index].slotHarqInf[harqIdx].hqState = HARQ_WAIT_FOR_FEEDBACK;
   slotInf[index].slotHarqInf[harqIdx].harqID = harqID;
   slotInf[index].numHARQ++;
}

/* =======================================================================================================*/
/*                                          Global API                                                    */
/* =======================================================================================================*/
sfnSlotInfo getSfnSlot()
{
   return gCurSlotSfn;
}


/* =======================================================================================================*/
/*                                          DL realted API                                                */
/* =======================================================================================================*/
void fillHARQFeedbackSlot(sfnSlotInfo *pSfnSlot, int8_t k1, int8_t harqID)
{
   if (harqInfo[harqID] != HARQ_FREE)
   {
      FATAL_PRINT;
      return;
   }
   harqInfo[harqID] = HARQ_WAIT_FOR_FEEDBACK;

   sfnSlotInfo dstSlotInfo;
   incSfnSlot(pSfnSlot, k1, &dstSlotInfo);

   if(pattern[dstSlotInfo.slot] == DL_SLOT)
   {
      FATAL_PRINT;
      return;
   }
#ifdef ENBALE_LOGS
   printf("[%4d/%2d] [GNB-DL] [%4d/%2d] harqID :%2d Feedback : [%4d/%2d]\n", 
         gCurSlotSfn.sfn, gCurSlotSfn.slot, pSfnSlot->sfn, pSfnSlot->slot, harqID, dstSlotInfo.sfn, dstSlotInfo.slot);
#endif
   fillSlotDb(&dstSlotInfo, harqID);
}

/* =======================================================================================================*/
/*                                          UE realted API                                                */
/* =======================================================================================================*/
void sendHarqAck(sfnSlotInfo *pSfnSlot)
{
#ifdef ENBALE_LOGS
   int8_t harqIDList[NUM_DL_HARQ] = {0};
   uint8_t indexCount = 0;
#endif
   uint8_t index = getSlotDbIndex(pSfnSlot);

   uint8_t harqCount = slotInf[index].numHARQ;
   for (int i = 0; i < harqCount;i++)
   {
      if (slotInf[index].slotHarqInf[i].hqState == HARQ_WAIT_FOR_FEEDBACK)
      {
         slotInf[index].slotHarqInf[i].hqState = HARQ_ACKED;
#ifdef ENBALE_LOGS
         harqIDList[indexCount++] = slotInf[index].slotHarqInf[i].harqID;
#endif
      }
   }

#ifdef ENBALE_LOGS
   if (indexCount > 0)
   {
      printf("[%4d/%2d] [UE    ] [%4d/%2d] harqID ", gCurSlotSfn.sfn, gCurSlotSfn.slot, pSfnSlot->sfn, pSfnSlot->slot);
      for (int i = 0; i < indexCount;i++)
      {
         printf("%d   ", harqIDList[i]);
      }
      printf("\n");
   }
#endif
}

/* =======================================================================================================*/
/*                                          UL realted API                                                */
/* =======================================================================================================*/
void processULFlow(sfnSlotInfo *pSfnSlot)
{
#ifdef ENBALE_LOGS
   int8_t harqIDList[NUM_DL_HARQ] = {0};
   uint8_t indexCount = 0;
#endif

   uint8_t index = getSlotDbIndex(pSfnSlot);

   uint8_t harqCount = slotInf[index].numHARQ;
   for (int i = 0; i < harqCount;i++)
   {
      if (slotInf[index].slotHarqInf[i].hqState == HARQ_ACKED)
      {
         uint8_t _harqId = slotInf[index].slotHarqInf[i].harqID;
         slotInf[index].slotHarqInf[i].hqState = HARQ_FREE;
         slotInf[index].numHARQ--;

         harqInfo[_harqId] = HARQ_FREE;
#ifdef ENBALE_LOGS
         harqIDList[indexCount++] = _harqId;
#endif
      }
   }
   if(slotInf[index].numHARQ != 0)
   {
      FATAL_PRINT;
   }

#ifdef ENBALE_LOGS
   if (indexCount > 0)
   {
      printf("[%4d/%2d] [GNB-UL] [%4d/%2d] harqID ", gCurSlotSfn.sfn, gCurSlotSfn.slot, pSfnSlot->sfn, pSfnSlot->slot);
      for (int i = 0; i < indexCount;i++)
      {
         printf("%d   ", harqIDList[i]);
      }
      printf("\n");
   }
#endif
}

/* =======================================================================================================*/
/*                                          Main Function                                                 */
/* =======================================================================================================*/
int main()
{
   initSystem();
   uint64_t totalDlSlotOcc = 0, harqMissOcc = 0;;
   while(1)
   {
      sfnSlotInfo curSfnSlot, duDlProcSfnSlot, duUlProcSfnSlot;
      curSfnSlot = getSfnSlot();
#ifdef ENABLE_STATS
      if (curSfnSlot.sfn == 0 && curSfnSlot.slot == 0)
      {
         static int count = 0;
         count++;
         if (count % 3 == 0) /* 30 sec stats */
         {
            printf("===============================================\n");
            printf("HARQ missed : (%llu/%llu) = %f\n", harqMissOcc, totalDlSlotOcc, (harqMissOcc*100*1.0)/totalDlSlotOcc);
            printf("===============================================\n");

            printf("===============================================\n");
            int i = 0;
            for (i = 0; i < 20; i++)
            {
               printf("Slot idx : %2d SucessCount : %5d and Fail Count : %5d\n", i, slotStats.harqSuccessCount[i], slotStats.harqFailCount[i]);
            }
            printf("===============================================\n");

            memset(&slotStats, 0, sizeof(nonPersistentStats));
         }
      }
#endif
      /* DL Flow */
      incSfnSlot(&curSfnSlot, TOTAL_DL_PROCESSING_DELAY, &duDlProcSfnSlot);
      if(pattern[duDlProcSfnSlot.slot] != UL_SLOT)
      {
         /* is HARQ availble? */
         int8_t harqID = getAvailHarQID();
         if (-1 == harqID)
         {
#ifdef ENBALE_LOGS
            printf("[%4d/%2d] HARQ buf not availble [%4d/%2d]\n", curSfnSlot.sfn, curSfnSlot.slot, duDlProcSfnSlot.sfn, duDlProcSfnSlot.slot);
#endif
            harqMissOcc++;
            slotStats.harqFailCount[duDlProcSfnSlot.slot]++;
         }
         else
         {
            /* get k1 */
            uint8_t k1 = getAvailK1(&duDlProcSfnSlot);

            /* Assign HARQ Feedback Slot */
            fillHARQFeedbackSlot(&duDlProcSfnSlot, k1, harqID);
            slotStats.harqSuccessCount[duDlProcSfnSlot.slot]++;
         }
         totalDlSlotOcc++;
      }

      /* UE processing */
      sendHarqAck(&curSfnSlot);

      /* UL Flow processing */
      decSfnSlot(&curSfnSlot, TOTAL_UL_PROCESSING_DELAY, &duUlProcSfnSlot);
      processULFlow(&duUlProcSfnSlot);

      //usleep(10000);
      incGlobalSfnSlot(1);
   }
   return 0;
}
