#include "Marlin.h"
#include "cardreader.h"
#include "ultralcd.h"
#include "stepper.h"
#include "temperature.h"
#include "language.h"

#ifdef SDSUPPORT



CardReader::CardReader()
{
   filesize = 0;
   sdpos = 0;
   sdprinting = false;
   cardOK = false;
   saving = false;
   logging = false;
   autostart_atmillis=0;
   workDirDepth = 0;
   file_subcall_ctr=0;
   memset(workDirParents, 0, sizeof(workDirParents));

   autostart_stilltocheck=true; //the SD start is delayed, because otherwise the serial cannot answer fast enough to make contact with the host software.
   lastnr=0;
  //power to SD reader
  #if SDPOWER > -1
    SET_OUTPUT(SDPOWER); 
    WRITE(SDPOWER,HIGH);
  #endif //SDPOWER
  
  autostart_atmillis=millis()+5000;
}

char *createFilename(char *buffer,const dir_t &p) //buffer>12characters
{
  char *pos=buffer;
  for (uint8_t i = 0; i < 11; i++) 
  {
    if (p.name[i] == ' ')continue;
    if (i == 8) 
    {
      *pos++='.';
    }
    *pos++=p.name[i];
  }
  *pos++=0;
  return buffer;
}


void CardReader::lsDive(const char *prepend, SdFile parent, const char * const match/*=NULL*/)
{
  dir_t p;
 uint8_t cnt=0;
 
  while (parent.readDir(p, longFilename) > 0)
  {
    if( DIR_IS_SUBDIR(&p) && lsAction!=LS_Count && lsAction!=LS_GetFilename) // hence LS_SerialPrint
    {

      char path[13*2];
      char lfilename[13];
      createFilename(lfilename,p);
      
      path[0]=0;
      if(strlen(prepend)==0) //avoid leading / if already in prepend
      {
       strcat(path,"/");
      }
      strcat(path,prepend);
      strcat(path,lfilename);
      strcat(path,"/");
      
      //Serial.print(path);
      
      SdFile dir;
      if(!dir.open(parent,lfilename, O_READ))
      {
        if(lsAction==LS_SerialPrint)
        {
          SERIAL_ECHO_START;
          SERIAL_ECHORPGM(MSG_SD_CANT_ENTER_SUBDIR);
          SERIAL_ECHOLN(lfilename);
        }
      }
      lsDive(path,dir);
      //close done automatically by destructor of SdFile

      
    }
    else
    {
      char pn0 = p.name[0];
      if (pn0 == DIR_NAME_FREE) break;
      if (pn0 == DIR_NAME_DELETED || pn0 == '.' || pn0 == '_') continue;
      char lf0 = longFilename[0];
      if (lf0 == '.' || lf0 == '_') continue;

      if (!DIR_IS_FILE_OR_SUBDIR(&p)) continue;
      // Ignore the files and directories with hidden or system attribute.
      if ((p.attributes & (DIR_ATT_HIDDEN | DIR_ATT_SYSTEM)) != 0) continue;
      filenameIsDir=DIR_IS_SUBDIR(&p);
      
      
      if(!filenameIsDir)
      {
        if(p.name[8]!='G') continue;
        if(p.name[9]=='~') continue;
      }
      //if(cnt++!=nr) continue;
      createFilename(filename,p);
      if(lsAction==LS_SerialPrint)
      {
        SERIAL_PROTOCOL(prepend);
        SERIAL_PROTOCOLLN(filename);
      }
      else if(lsAction==LS_Count)
      {
        nrFiles++;
      } 
      else if(lsAction==LS_GetFilename)
      {
        if (match != NULL) {
          if (strcasecmp(match, filename) == 0) return;
        }
        else if (cnt == nrFiles) return;
        cnt++;
        
      }
    }
  }
}

void CardReader::ls() 
{
  lsAction=LS_SerialPrint;
  if(lsAction==LS_Count)
  nrFiles=0;

  root.rewind();
  lsDive("",root);
}


void CardReader::initsd()
{
  cardOK = false;
  if(root.isOpen())
    root.close();
#ifdef SDSLOW
  if (!card.init(SPI_HALF_SPEED,SDSS)
  #if defined(LCD_SDSS) && (LCD_SDSS != SDSS)
    && !card.init(SPI_HALF_SPEED,LCD_SDSS)
  #endif
    )
#else
  if (!card.init(SPI_FULL_SPEED,SDSS)
  #if defined(LCD_SDSS) && (LCD_SDSS != SDSS)
    && !card.init(SPI_FULL_SPEED,LCD_SDSS)
  #endif
    )
#endif
  {
    //if (!card.init(SPI_HALF_SPEED,SDSS))
    SERIAL_ECHO_START;
    SERIAL_ECHOLNRPGM(MSG_SD_INIT_FAIL);
  }
  else if (!volume.init(&card))
  {
    SERIAL_ERROR_START;
    SERIAL_ERRORLNRPGM(MSG_SD_VOL_INIT_FAIL);
  }
  else if (!root.openRoot(&volume)) 
  {
    SERIAL_ERROR_START;
    SERIAL_ERRORLNRPGM(MSG_SD_OPENROOT_FAIL);
  }
  else 
  {
    cardOK = true;
    SERIAL_ECHO_START;
    SERIAL_ECHOLNRPGM(MSG_SD_CARD_OK);
  }
  workDir=root;
  curDir=&root;
  /*
  if(!workDir.openRoot(&volume))
  {
    SERIAL_ECHOLNPGM(MSG_SD_WORKDIR_FAIL);
  }
  */
  
}

void CardReader::setroot()
{
  /*if(!workDir.openRoot(&volume))
  {
    SERIAL_ECHOLNPGM(MSG_SD_WORKDIR_FAIL);
  }*/
  workDir=root;
  
  curDir=&workDir;
}
void CardReader::release()
{
  sdprinting = false;
  cardOK = false;
}

void CardReader::startFileprint()
{
  if(cardOK)
  {
    sdprinting = true;
  }
}

void CardReader::pauseSDPrint()
{
  if(sdprinting)
  {
    sdprinting = false;
  }
}


void CardReader::openLogFile(char* name)
{
  logging = true;
  openFile(name, false);
}

void CardReader::getAbsFilename(char *t)
{
  uint8_t cnt=0;
  *t='/';t++;cnt++;
  for(uint8_t i=0;i<workDirDepth;i++)
  {
    workDirParents[i].getFilename(t); //SDBaseFile.getfilename!
    while(*t!=0 && cnt< MAXPATHNAMELENGTH) 
    {t++;cnt++;}  //crawl counter forward.
  }
  if(cnt<MAXPATHNAMELENGTH-13)
    file.getFilename(t);
  else
    t[0]=0;
}

void CardReader::openFile(char* name,bool read, bool replace_current/*=true*/)
{
  if(!cardOK)
    return;
  if(file.isOpen())  //replacing current file by new file, or subfile call
  {
    if(!replace_current)
    {
     if((int)file_subcall_ctr>(int)SD_PROCEDURE_DEPTH-1)
     {
       SERIAL_ERROR_START;
       SERIAL_ERRORPGM("trying to call sub-gcode files with too many levels. MAX level is:");
       SERIAL_ERRORLN(SD_PROCEDURE_DEPTH);
       kill();
       return;
     }
     
     SERIAL_ECHO_START;
     SERIAL_ECHOPGM("SUBROUTINE CALL target:\"");
     SERIAL_ECHO(name);
     SERIAL_ECHOPGM("\" parent:\"");
     
     //store current filename and position
     getAbsFilename(filenames[file_subcall_ctr]);
     
     SERIAL_ECHO(filenames[file_subcall_ctr]);
     SERIAL_ECHOPGM("\" pos");
     SERIAL_ECHOLN(sdpos);
     filespos[file_subcall_ctr]=sdpos;
     file_subcall_ctr++;
    }
    else
    {
     SERIAL_ECHO_START;
     SERIAL_ECHOPGM("Now doing file: ");
     SERIAL_ECHOLN(name);
    }
    file.close();
  }
  else //opening fresh file
  {
    file_subcall_ctr=0; //resetting procedure depth in case user cancels print while in procedure
    SERIAL_ECHO_START;
    SERIAL_ECHOPGM("Now fresh file: ");
    SERIAL_ECHOLN(name);
  }
  sdprinting = false;
  
 
  SdFile myDir;
  curDir=&root;
  char *fname=name;
  
  char *dirname_start,*dirname_end;
  if(name[0]=='/')
  {
    dirname_start=strchr(name,'/')+1;
    while(dirname_start>0)
    {
      dirname_end=strchr(dirname_start,'/');
      //SERIAL_ECHO("start:");SERIAL_ECHOLN((int)(dirname_start-name));
      //SERIAL_ECHO("end  :");SERIAL_ECHOLN((int)(dirname_end-name));
      if(dirname_end>0 && dirname_end>dirname_start)
      {
        char subdirname[13];
        strncpy(subdirname, dirname_start, dirname_end-dirname_start);
        subdirname[dirname_end-dirname_start]=0;
        SERIAL_ECHOLN(subdirname);
        if(!myDir.open(curDir,subdirname,O_READ))
        {
          SERIAL_PROTOCOLRPGM(MSG_SD_OPEN_FILE_FAIL);
          SERIAL_PROTOCOL(subdirname);
          SERIAL_PROTOCOLLNPGM(".");
          return;
        }
        else
        {
          //SERIAL_ECHOLN("dive ok");
        }
          
        curDir=&myDir; 
        dirname_start=dirname_end+1;
      }
      else // the reminder after all /fsa/fdsa/ is the filename
      {
        fname=dirname_start;
        //SERIAL_ECHOLN("remaider");
        //SERIAL_ECHOLN(fname);
        break;
      }
      
    }
  }
  else //relative path
  {
    curDir=&workDir;
  }
  if(read)
  {
    if (file.open(curDir, fname, O_READ)) 
    {
      filesize = file.fileSize();
      SERIAL_PROTOCOLRPGM(MSG_SD_FILE_OPENED);
      SERIAL_PROTOCOL(fname);
      SERIAL_PROTOCOLRPGM(MSG_SD_SIZE);
      SERIAL_PROTOCOLLN(filesize);
      sdpos = 0;
      
      SERIAL_PROTOCOLLNRPGM(MSG_SD_FILE_SELECTED);
      getfilename(0, fname);
      lcd_setstatus(longFilename[0] ? longFilename : fname);
      lcd_setstatus("SD-PRINTING         ");
    }
    else
    {
      SERIAL_PROTOCOLRPGM(MSG_SD_OPEN_FILE_FAIL);
      SERIAL_PROTOCOL(fname);
      SERIAL_PROTOCOLLNPGM(".");
    }
  }
  else 
  { //write
    if (!file.open(curDir, fname, O_CREAT | O_APPEND | O_WRITE | O_TRUNC))
    {
      SERIAL_PROTOCOLRPGM(MSG_SD_OPEN_FILE_FAIL);
      SERIAL_PROTOCOL(fname);
      SERIAL_PROTOCOLLNPGM(".");
    }
    else
    {
      saving = true;
      SERIAL_PROTOCOLRPGM(MSG_SD_WRITE_TO_FILE);
      SERIAL_PROTOCOLLN(name);
      lcd_setstatus(fname);
    }
  }
  
}

void CardReader::removeFile(char* name)
{
  if(!cardOK)
    return;
  file.close();
  sdprinting = false;
  
  
  SdFile myDir;
  curDir=&root;
  char *fname=name;
  
  char *dirname_start,*dirname_end;
  if(name[0]=='/')
  {
    dirname_start=strchr(name,'/')+1;
    while(dirname_start>0)
    {
      dirname_end=strchr(dirname_start,'/');
      //SERIAL_ECHO("start:");SERIAL_ECHOLN((int)(dirname_start-name));
      //SERIAL_ECHO("end  :");SERIAL_ECHOLN((int)(dirname_end-name));
      if(dirname_end>0 && dirname_end>dirname_start)
      {
        char subdirname[13];
        strncpy(subdirname, dirname_start, dirname_end-dirname_start);
        subdirname[dirname_end-dirname_start]=0;
        SERIAL_ECHOLN(subdirname);
        if(!myDir.open(curDir,subdirname,O_READ))
        {
          SERIAL_PROTOCOLRPGM("open failed, File: ");
          SERIAL_PROTOCOL(subdirname);
          SERIAL_PROTOCOLLNPGM(".");
          return;
        }
        else
        {
          //SERIAL_ECHOLN("dive ok");
        }
          
        curDir=&myDir; 
        dirname_start=dirname_end+1;
      }
      else // the reminder after all /fsa/fdsa/ is the filename
      {
        fname=dirname_start;
        //SERIAL_ECHOLN("remaider");
        //SERIAL_ECHOLN(fname);
        break;
      }
      
    }
  }
  else //relative path
  {
    curDir=&workDir;
  }
    if (file.remove(curDir, fname)) 
    {
      SERIAL_PROTOCOLPGM("File deleted:");
      SERIAL_PROTOCOLLN(fname);
      sdpos = 0;
    }
    else
    {
      SERIAL_PROTOCOLPGM("Deletion failed, File: ");
      SERIAL_PROTOCOL(fname);
      SERIAL_PROTOCOLLNPGM(".");
    }
  
}

void CardReader::getStatus()
{
  if(sdprinting){
    SERIAL_PROTOCOL(longFilename);
    SERIAL_PROTOCOLPGM("\n");
    SERIAL_PROTOCOLRPGM(MSG_SD_PRINTING_BYTE);
    SERIAL_PROTOCOL(sdpos);
    SERIAL_PROTOCOLPGM("/");
    SERIAL_PROTOCOLLN(filesize);
    uint16_t time = millis()/60000 - starttime/60000;
    SERIAL_PROTOCOL(itostr2(time/60));
    SERIAL_PROTOCOL(':');
    SERIAL_PROTOCOL(itostr2(time%60));
    SERIAL_PROTOCOLPGM("\n");
  }
  else{
    SERIAL_PROTOCOLLNRPGM("Not printing");
  }
}
void CardReader::write_command(char *buf)
{
  char* begin = buf;
  char* npos = 0;
  char* end = buf + strlen(buf) - 1;

  file.writeError = false;
  if((npos = strchr(buf, 'N')) != NULL)
  {
    begin = strchr(npos, ' ') + 1;
    end = strchr(npos, '*') - 1;
  }
  end[1] = '\r';
  end[2] = '\n';
  end[3] = '\0';
  file.write(begin);
  if (file.writeError)
  {
    SERIAL_ERROR_START;
    SERIAL_ERRORLNRPGM(MSG_SD_ERR_WRITE_TO_FILE);
  }
}

#define CHUNK_SIZE 64

void CardReader::write_command_no_newline(char *buf)
{
  file.write(buf, CHUNK_SIZE);
  if (file.writeError)
  {
    SERIAL_ERROR_START;
    SERIAL_ERRORLNRPGM(MSG_SD_ERR_WRITE_TO_FILE);
    MYSERIAL.println("An error while writing to the SD Card.");
  }
}


void CardReader::checkautostart(bool force)
{
  if(!force)
  {
    if(!autostart_stilltocheck)
      return;
    if(autostart_atmillis<millis())
      return;
  }
  autostart_stilltocheck=false;
  if(!cardOK)
  {
    initsd();
    if(!cardOK) //fail
      return;
  }
  
  char autoname[30];
  sprintf_P(autoname, PSTR("auto%i.g"), lastnr);
  for(int8_t i=0;i<(int8_t)strlen(autoname);i++)
    autoname[i]=tolower(autoname[i]);
  dir_t p;

  root.rewind();
  
  bool found=false;
  while (root.readDir(p, NULL) > 0) 
  {
    for(int8_t i=0;i<(int8_t)strlen((char*)p.name);i++)
    p.name[i]=tolower(p.name[i]);
    //Serial.print((char*)p.name);
    //Serial.print(" ");
    //Serial.println(autoname);
    if(p.name[9]!='~') //skip safety copies
    if(strncmp((char*)p.name,autoname,5)==0)
    {
      char cmd[30];
      // M23: Select SD file
      sprintf_P(cmd, PSTR("M23 %s"), autoname);
      enquecommand(cmd);
      // M24: Start/resume SD print
      enquecommand_P(PSTR("M24"));
      found=true;
    }
  }
  if(!found)
    lastnr=-1;
  else
    lastnr++;
}

void CardReader::closefile(bool store_location)
{
  file.sync();
  file.close();
  saving = false; 
  logging = false;
  
  if(store_location)
  {
    //future: store printer state, filename and position for continuing a stopped print
    // so one can unplug the printer and continue printing the next day.
    
  }

  
}

void CardReader::getfilename(uint16_t nr, const char * const match/*=NULL*/)
{
  curDir=&workDir;
  lsAction=LS_GetFilename;
  nrFiles=nr;
  curDir->rewind();
  lsDive("",*curDir,match);
  
}

uint16_t CardReader::getnrfilenames()
{
  curDir=&workDir;
  lsAction=LS_Count;
  nrFiles=0;
  curDir->rewind();
  lsDive("",*curDir);
  //SERIAL_ECHOLN(nrFiles);
  return nrFiles;
}

void CardReader::chdir(const char * relpath)
{
  SdFile newfile;
  SdFile *parent=&root;
  
  if(workDir.isOpen())
    parent=&workDir;
  
  if(!newfile.open(*parent,relpath, O_READ))
  {
   SERIAL_ECHO_START;
   SERIAL_ECHORPGM(MSG_SD_CANT_ENTER_SUBDIR);
   SERIAL_ECHOLN(relpath);
  }
  else
  {
    if (workDirDepth < MAX_DIR_DEPTH) {
      for (int d = ++workDirDepth; d--;)
        workDirParents[d+1] = workDirParents[d];
      workDirParents[0]=*parent;
    }
    workDir=newfile;
  }
}

void CardReader::updir()
{
  if(workDirDepth > 0)
  {
    --workDirDepth;
    workDir = workDirParents[0];
    int d;
    for (int d = 0; d < workDirDepth; d++)
      workDirParents[d] = workDirParents[d+1];
  }
}


void CardReader::printingHasFinished()
{
    st_synchronize();
    if(file_subcall_ctr>0) //heading up to a parent file that called current as a procedure.
    {
      file.close();
      file_subcall_ctr--;
      openFile(filenames[file_subcall_ctr],true,true);
      setIndex(filespos[file_subcall_ctr]);
      startFileprint();
    }
    else
    {
      quickStop();
      file.close();
      sdprinting = false;
      if(SD_FINISHED_STEPPERRELEASE)
      {
          //finishAndDisableSteppers();
          enquecommand_P(PSTR(SD_FINISHED_RELEASECOMMAND));
      }
      autotempShutdown();
    }
}

bool CardReader::ToshibaFlashAir_GetIP(uint8_t *ip)
{
    memset(ip, 0, 4);
    return card.readExtMemory(1, 1, 0x400+0x150, 4, ip);
}

#endif //SDSUPPORT
