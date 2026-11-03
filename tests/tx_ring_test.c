#include "dmod.h"
#include "dmeth_port.h"
#include "port/stm32_common_regs.h"
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
static ETH_TypeDef fake_eth;
#undef ETH
#define ETH (&fake_eth)
static void* interleaved_copy(void*,const void*,size_t);
#define memcpy interleaved_copy
#include "../src/port/stm32_common/stm32_common.c"
#undef memcpy

static pthread_mutex_t tx_lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t gate=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t changed=PTHREAD_COND_INITIALIZER;
static _Thread_local int sender;
static bool copying,second_waiting,second_done;
static unsigned unlocks;
static uint32_t tick;
static eth_dma_desc_t descriptors[2];
static uint8_t buffers[2][64],frames[2][64];
static int results[2];
static int lock_tx(dmosi_mutex_t mutex)
{
    if(sender==2)
    {
        pthread_mutex_lock(&gate);second_waiting=true;pthread_cond_broadcast(&changed);pthread_mutex_unlock(&gate);
    }
    return pthread_mutex_lock((pthread_mutex_t*)mutex);
}
static int unlock_tx(dmosi_mutex_t mutex) { unlocks++;return pthread_mutex_unlock((pthread_mutex_t*)mutex); }
static uint32_t get_tick(void) { return tick++; }
static void* interleaved_copy(void* dst,const void* src,size_t size)
{
    if(sender==1 && dst==buffers[0])
    {
        pthread_mutex_lock(&gate);copying=true;pthread_cond_broadcast(&changed);
        while(!second_waiting && !second_done)pthread_cond_wait(&changed,&gate);
        pthread_mutex_unlock(&gate);
    }
    return memcpy(dst,src,size);
}
static void* transmit(void* arg)
{
    sender=(int)(uintptr_t)arg;
    if(sender==2)
    {
        pthread_mutex_lock(&gate);while(!copying)pthread_cond_wait(&changed,&gate);pthread_mutex_unlock(&gate);
    }
    results[sender-1]=dmeth_port_transmit_frame(0,frames[sender-1],64);
    if(sender==2)
    {
        pthread_mutex_lock(&gate);second_done=true;pthread_cond_broadcast(&changed);pthread_mutex_unlock(&gate);
    }
    return NULL;
}
static void setup(void)
{
    memset(s_eth,0,sizeof(s_eth));memset(descriptors,0,sizeof(descriptors));memset(buffers,0,sizeof(buffers));
    for(unsigned i=0;i<2;i++){descriptors[i].buffer=buffers[i];memset(frames[i],(int)(0x11U*(i+1U)),64);}
    s_eth[0].initialized=s_eth[0].running=s_eth[0].link_configured=true;
    s_eth[0].tx_count=2;s_eth[0].tx_desc=descriptors;s_eth[0].tx_mutex=(dmosi_mutex_t)&tx_lock;
    dmosi_mutex_lock=lock_tx;dmosi_mutex_unlock=unlock_tx;dmosi_get_tick_count=get_tick;
    copying=second_waiting=second_done=false;unlocks=0;tick=0;
}
static int concurrent_senders(void)
{
    setup();pthread_t first,second;
    pthread_create(&first,NULL,transmit,(void*)1);pthread_create(&second,NULL,transmit,(void*)2);
    pthread_join(first,NULL);pthread_join(second,NULL);
    bool good=results[0]==0 && results[1]==0 && s_eth[0].tx_head==0 && unlocks==2;
    for(unsigned i=0;i<2;i++)
    {
        good=good && (descriptors[i].status&ETH_DMA_DESC_OWN)!=0 && descriptors[i].ctrl_size==64;
        for(unsigned j=0;j<64;j++)good=good && buffers[i][j]==frames[i][j];
    }
    if(!good)fprintf(stderr,"concurrent senders overwrote a descriptor or left a TX hole\n");
    return !good;
}
static int timeout_releases_reservation(void)
{
    setup();sender=0;s_eth[0].io_timeout_ms=1;descriptors[0].status=ETH_DMA_DESC_OWN;
    if(dmeth_port_transmit_frame(0,frames[0],64)!=-ETIMEDOUT || unlocks!=1 || s_eth[0].tx_head!=0)return 1;
    descriptors[0].status=0;
    if(dmeth_port_transmit_frame(0,frames[0],64)!=0 || unlocks!=2 || s_eth[0].tx_head!=1)return 1;
    return 0;
}
int main(void)
{
    int errors=concurrent_senders()+timeout_releases_reservation();
    printf("TX DMA concurrency regression: %d failure(s)\n",errors);return errors;
}
