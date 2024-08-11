/*

compile: cc -o sht30 sht30.c

public domain by Shaddack

do what you wish

*/

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <string.h>


#define SHT30_ADDR  0x44  // 0b 100 0100   ADDR=L (default)
#define SHT30_ADDR2 0x45  // 0b 100 0101   ADDR=H

#define SHT30_RESET      0x30A2
#define SHT30_HEATER_ON  0x306D
#define SHT30_HEATER_OFF 0x3066
#define SHT30_GETSTATUS  0xF32D
#define SHT30_CLRSTATUS  0x3041
#define SHT30_ART        0x2B32  // accelerated response time, 4 Hz
#define SHT30_READPER    0xE000  // for periodic modes
#define SHT30_STOP       0x3093  // stop periodic measurements
#define SHT30_READSINGLE 0x2400  // singleshot, no clock stretch, high repeatability
#define SHT30_READSTART  0x2032  // 0.5Hz, high repeatability


#define I2C_DEVICE "/dev/i2c-1"   // raspberry pi default


int dev_addr=SHT30_ADDR;

int verbose=0;
int lowlight=0;
int overflowtest=0;
char i2cdevname[256];

#define MAXBUF 8
char buf[MAXBUF];

void i2c_setaddr(int addr){
  if(addr)dev_addr=SHT30_ADDR2;else dev_addr=SHT30_ADDR;
}


void i2c_writecmd(int file,unsigned int cmd){
  unsigned char buf[2];
  buf[0]=cmd>>8;
  buf[1]=cmd;
  if(verbose)fprintf(stderr,"raw write: %02x %02x\n",buf[0],buf[1]);
  if (write(file,buf,2) != 2) {
    /* ERROR HANDLING: i2c transaction failed */
    perror("Failed to write 2 bytes to the i2c bus");
    fprintf(stderr,"Cannot talk to addr 0x%02x on bus '%s'.\n",dev_addr,i2cdevname);
    exit(1);
  }
}


void i2c_readbuf(int file,int len){
  if(verbose)fprintf(stderr,"raw read: %d bytes\n",len);
  if (read(file,buf,len) != len) {
    /* ERROR HANDLING: i2c transaction failed */
    perror("Failed to read from the i2c bus");
    fprintf(stderr,"Cannot talk to addr 0x%02x on bus '%s'.\n",dev_addr,i2cdevname);
    exit(1);
  }
  if(verbose){
    fprintf(stderr,"raw read:");
    for(int t=0;t<len;t++)fprintf(stderr," %02x",buf[t]);
    fprintf(stderr,"\n");
  }
}


void readsht30(int cmd,int readlen,int isdelay){
  int file;
  float lux;

  if(verbose)fprintf(stderr,"accessing device 0x%02x on %s\n",dev_addr,i2cdevname);

  if ((file = open(i2cdevname, O_RDWR)) < 0) {
    /* ERROR HANDLING: you can check errno to see what went wrong */
    perror("Failed to open the i2c bus");
    fprintf(stderr,"bus '%s'\n",i2cdevname);
    exit(1);
  }

  if (ioctl(file, I2C_SLAVE, dev_addr) < 0) {
    /* ERROR HANDLING; you can check errno to see what went wrong */
    perror("IOCTL error on i2c bus");
    fprintf(stderr,"Failed to acquire bus access and/or talk to slave at 0x%02x.\n",dev_addr);
    exit(1);
  }

  i2c_writecmd(file,cmd);
  if(isdelay)usleep(250000);
  if(readlen)i2c_readbuf(file,readlen);

  close(file);
}


#define OUTMODE_ALL  0
#define OUTMODE_TEMP 1
#define OUTMODE_HUMI 2
#define OUTMODE_TEMPHUMI 3
#define OUTMODE_JSON 4

void printtemphumi(int fahr,int outmode,int outint){
  int rawtemp = (buf[0] * 256 + buf[1]);
  float temp;
  if(fahr)temp = -49 + (315 * rawtemp / 65535.0);
  else temp = -45 + (175 * rawtemp / 65535.0);
  float humi = 100 * (buf[3] * 256 + buf[4]) / 65535.0;
  // Output data to screen

  if(outmode==OUTMODE_JSON){
    printf("{\"humi\":%.2f,\"temp\":%.2f}",humi,temp);
    return;
  }

  if(outint){
    if(temp>=0)temp=temp+0.49;else temp=temp-0.51;
    if(humi>=0)humi=humi+0.49;else humi=humi-0.51;
    if(outmode==OUTMODE_TEMP){printf("%d\n",(int)temp);}
    else if(outmode==OUTMODE_HUMI){printf("%d\n",(int)humi);}
    else if(outmode==OUTMODE_TEMPHUMI){printf("%d %d\n",(int)humi,(int)temp);}
    else {
      printf("Humidity   : %d %\n", (int)humi);
      printf("Temperature: %d '%c\n", (int)temp, fahr?'F':'C');
    }
  }else
  {
    if(outmode==OUTMODE_TEMP){printf("%.2f\n",temp);}
    else if(outmode==OUTMODE_HUMI){printf("%.2f\n",humi);}
    else if(outmode==OUTMODE_TEMPHUMI){printf("%.2f %.2f\n",humi,temp);}
    else {
      printf("Humidity   : %.2f %\n", humi);
      printf("Temperature: %.2f '%c\n", temp, fahr?'F':'C');
    }
  }
}

#define READMODE_SINGLE 0
#define READMODE_STARTPER 1
#define READMODE_PERIODIC 2

void readtemphumi(int mode,int fahr,int outmode,int outint){
  if(mode==READMODE_PERIODIC)readsht30(SHT30_READPER,6,1);
  else if(mode==READMODE_STARTPER)readsht30(SHT30_READSTART,6,1);
  else readsht30(SHT30_READSINGLE,6,1);

//  readsht30(SHT30_READSTART,6,1);
  printtemphumi(fahr,outmode,outint);
}



void printstatusitem(char*s,int i){
//  printf("  %-15s: %s\n",s,gettf(i));
  printf("  %-15s: %s\n",s, i?"TRUE":"false" );
}

void readstatusword(){
  if(verbose){fprintf(stderr,"reading status word\n");}
  readsht30(SHT30_GETSTATUS,2,0);
  printf("Status: %02x%02x\n",buf[0],buf[1]);
  printstatusitem("pending alert" ,buf[0]&0x80);
  printstatusitem("heater enabled",buf[0]&0x20);
  printstatusitem("humi alert"    ,buf[0]&0x08);
  printstatusitem("temp alert"    ,buf[0]&0x04);
  printstatusitem("read periodic" ,buf[1]&0x20); // not in datasheet?
  printstatusitem("reset detect"  ,buf[1]&0x10);
  printstatusitem("command fail"  ,buf[1]&0x02);
  printstatusitem("checksum fail" ,buf[1]&0x01);
}


void clrstatusword(){
  if(verbose){fprintf(stderr,"clearing status word\n");}
  readsht30(SHT30_CLRSTATUS,0,0);
}

void resetchip(){
  if(verbose){fprintf(stderr,"resetting chip\n");}
  readsht30(SHT30_RESET,0,0);
}

void setheater(int on){
  if(verbose){fprintf(stderr,"setting heater to %i\n",on);}
  if(on)readsht30(SHT30_HEATER_ON ,0,0);
  else  readsht30(SHT30_HEATER_OFF,0,0);
}



void help(){
  printf("SHT3x sensor read\n");
  printf("Usage: sht30 [-h<0|1>] [-i] [-r<h|t|ht>] [-j] [-s] [-sc] [-a0] [-a1] [-d <dev>] [-v] \n");
  printf("measure:\n");
  printf("  -i        integer mode (no float)\n");
  printf("  -rh       output single-line humidity\n");
  printf("  -rt       output single-line temperature\n");
  printf("  -rht      output single-line humidity and temperature\n");
  printf("  -j        JSON format\n");
  printf("  -nr       do not read humidity/temp\n");
  printf("chip setting:\n");
  printf("  -h1       on-chip heater enable\n");
  printf("  -h0       on-chip heater disable\n");
  printf("  -s        read status word\n");
  printf("  -sc       clear status word\n");
  printf("  -R        reset chip\n");
  printf("I2C:\n");
  printf("  -a0       address ADDR=L (0x%02x, default)\n",SHT30_ADDR);
  printf("  -a1       address ADDR=H (0x%02x)\n",SHT30_ADDR2);
  printf("  -d <dev>  specify I2C device, default %s\n",I2C_DEVICE);
  printf("general:\n");
  printf("  -v        verbose mode\n");
  printf("  -h,--help this help\n");
  printf("\n");
}


int main(int argc,char*argv[]){
  float lux;
  int t;

  int heater=-1;
  int clrstatus=0;
  int getstatus=0;
  int gettemp=1;
  int fahrenheit=0;
  int readmode=0;
  int outmode=0;
  int outint=0;
  int readrht=1;
  int doreset=0;

  strcpy(i2cdevname,I2C_DEVICE);

  for(t=1;t<argc;t++){
    // meas
    if(!strcmp(argv[t],"-i")){outint=1;continue;}
    if(!strcmp(argv[t],"-rh")){outmode=OUTMODE_HUMI;continue;}
    if(!strcmp(argv[t],"-rt")){outmode=OUTMODE_TEMP;continue;}
    if(!strcmp(argv[t],"-rht")){outmode=OUTMODE_TEMPHUMI;continue;}
    if(!strcmp(argv[t],"-j")){outmode=OUTMODE_JSON;continue;}
    if(!strcmp(argv[t],"-nr")){readrht=0;;continue;}
    // chip setting
    if(!strcmp(argv[t],"-h1")){heater=1;continue;}
    if(!strcmp(argv[t],"-h0")){heater=0;continue;}
    if(!strcmp(argv[t],"-s")){getstatus=1;continue;}
    if(!strcmp(argv[t],"-sc")){clrstatus=1;continue;}
    if(!strcmp(argv[t],"-R")){doreset=1;continue;}
    // i2c
    if(!strcmp(argv[t],"-a1")){i2c_setaddr(1);continue;}
    if(!strcmp(argv[t],"-a0")){i2c_setaddr(0);continue;}
    if(!strcmp(argv[t],"-d")){if(t+1<argc)strncpy(i2cdevname,argv[t+1],255);i2cdevname[255]=0;t++;continue;}
    // gen
    if(!strcmp(argv[t],"-v")){verbose=1;continue;}
    if(!strcmp(argv[t],"-h")){help();exit(0);}
    if(!strcmp(argv[t],"--help")){help();exit(0);}
    // err
    fprintf(stderr,"ERR: unknown parameter: %s\n",argv[t]);
    help();
    exit(1);
  }


  if(doreset)resetchip();

  if(heater>=0)setheater(heater);

  if(readrht)readtemphumi(readmode,fahrenheit,outmode,outint);

//  readstatusbyte();
  if(clrstatus)clrstatusword();
  if(getstatus)readstatusword();

//  if(isint==2) printf("%d\n",(int)10*(lux+0.49));
//  if(isint==1) printf("%d\n",(int)(lux+0.49));
//  else printf("%.1f\n",lux);
}



