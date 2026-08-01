/**
 * Furnace Tracker - multi-system chiptune tracker
 * Copyright (C) 2021-2026 tildearrow and contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "engine.h"
#include "../ta-log.h"
#ifdef HAVE_SNDFILE
#include "sfWrapper.h"
#endif

#define EXPORT_BUFSIZE 2048

void _runExportThread(DivEngine* caller) {
  caller->runExportThread();
}

bool DivEngine::isExporting() {
  return exporting;
}

void DivEngine::getLoopsLeft(int &loops) {
  if (totalLoops<0 || exportLoopCount==0) {
    loops=0;
    return;
  }
  loops=exportLoopCount-1-totalLoops;
}

void DivEngine::getTotalLoops(int &loops) {
  loops=exportLoopCount-1;
}

void DivEngine::getCurSongPos(int &row, int &order) {
  row=curRow;
  order=curOrder;
}

void DivEngine::getTotalAudioFiles(int &files) {
  files=0;

  switch (exportMode) {
    case DIV_EXPORT_MODE_ONE: {
      files=1;
      break;
    }
    case DIV_EXPORT_MODE_MANY_SYS: {
      files=1; // there actually are several files but they are processed in the same loop, so to correctly draw progress we think of them as one file
      break;
    }
    case DIV_EXPORT_MODE_MANY_CHAN: {
      for (int i=0; i<song.chans; i++) {
        if (!exportChannelMask[i]) continue;

        files++;

        if (getChannelType(i)==5) {
          i++;
          while (true) {
            if (i>=song.chans) break;
            if (getChannelType(i)!=5) break;
            i++;
          }
          i--;
        }
      }
      break;
    }
    default:
      break;
  }
}

void DivEngine::getCurFileIndex(int &file) {
  file=0;

  switch (exportMode) {
    case DIV_EXPORT_MODE_ONE: {
      file=0;
      break;
    }
    case DIV_EXPORT_MODE_MANY_SYS: {
      file=0; // there actually are several files but they are processed in the same loop, so to correctly draw progress we think of them as one file
      break;
    }
    case DIV_EXPORT_MODE_MANY_CHAN: {
      file=curExportChan;
      break;
    }
    default:
      break;
  }
}

bool DivEngine::getIsFadingOut() {
  return isFadingOut;
}

#ifdef HAVE_SNDFILE

#define MAP_BITRATE \
  if (exportFormat!=DIV_EXPORT_FORMAT_WAV) { \
    double mappedLevel=0.0; \
\
    switch (exportFormat) { \
      case DIV_EXPORT_FORMAT_OPUS: \
        mappedLevel=1.0-((double)((exportBitRate/(double)MAX(1,exportOutputs))-6000.0)/250000.0); \
        break; \
      case DIV_EXPORT_FORMAT_FLAC: \
        mappedLevel=exportVBRQuality*0.125; \
        break; \
      case DIV_EXPORT_FORMAT_VORBIS: \
        mappedLevel=1.0-(exportVBRQuality*0.1); \
        break; \
      case DIV_EXPORT_FORMAT_MPEG_L3: { \
        int mappedBitRateMode=SF_BITRATE_MODE_CONSTANT; \
        switch (exportBitRateMode) { \
          case DIV_EXPORT_BITRATE_CONSTANT: \
            mappedBitRateMode=SF_BITRATE_MODE_CONSTANT; \
            break; \
          case DIV_EXPORT_BITRATE_VARIABLE: \
            mappedBitRateMode=SF_BITRATE_MODE_VARIABLE; \
            break; \
          case DIV_EXPORT_BITRATE_AVERAGE: \
            mappedBitRateMode=SF_BITRATE_MODE_AVERAGE; \
            break; \
        } \
        if (exportBitRateMode==DIV_EXPORT_BITRATE_VARIABLE) { \
          mappedLevel=1.0-(exportVBRQuality*0.1); \
        } else { \
          if (got.rate>=32000) { \
            mappedLevel=(320000.0-(double)exportBitRate)/288000.0; \
          } else if (got.rate>=16000) { \
            mappedLevel=(160000.0-(double)exportBitRate)/152000.0; \
          } else { \
            mappedLevel=(64000.0-(double)exportBitRate)/56000.0; \
          } \
        } \
\
        if (sf_command(sf,SFC_SET_BITRATE_MODE,&mappedBitRateMode,sizeof(mappedBitRateMode))==SF_FALSE) { \
          logE("could not set bit rate mode! (%s)",sf_strerror(sf)); \
        } \
        break; \
      } \
      default: \
        break; \
    } \
\
    if (sf_command(sf,SFC_SET_COMPRESSION_LEVEL,&mappedLevel,sizeof(mappedLevel))!=SF_TRUE) { \
      logE("could not set compression level! (%s)",sf_strerror(sf)); \
    } \
  }

void applyExportMetadata(SNDFILE* sf, const DivSong& song, DivAudioExportModes mode, int index = 0) {
  if (sf == nullptr) return;

  // 1. TITLE
  String titleStr = song.name;
  if (titleStr.empty()) titleStr = "Untitled";

  if (mode == DIV_EXPORT_MODE_MANY_CHAN) {
    titleStr += fmt::sprintf(" (Channel %d)", index + 1);
  } else if (mode == DIV_EXPORT_MODE_MANY_SYS) {
    titleStr += fmt::sprintf(" (System %d)", index + 1);
  }
  sf_set_string(sf, SF_STR_TITLE, titleStr.c_str());

  // 2. ARTIST / COMPOSER
  if (!song.author.empty()) {
    sf_set_string(sf, SF_STR_ARTIST, song.author.c_str());
  } else if (!song.composer.empty()) {
    sf_set_string(sf, SF_STR_ARTIST, song.composer.c_str());
  }

  // 3. ALBUM / GAME NAME
  if (!song.category.empty()) {
    sf_set_string(sf, SF_STR_ALBUM, song.category.c_str());
  }

  // 4. TRACK NUMBER
  if (mode == DIV_EXPORT_MODE_MANY_CHAN || mode == DIV_EXPORT_MODE_MANY_SYS) {
    sf_set_string(sf, SF_STR_TRACKNUMBER, fmt::sprintf("%d", index + 1).c_str());
  }

  // 5. GENRE
  sf_set_string(sf, SF_STR_GENRE, "Chiptune");

  // 6. DATE & COPYRIGHT
  if (!song.createdDate.empty()) sf_set_string(sf, SF_STR_DATE, song.createdDate.c_str());
  if (!song.copyright.empty()) sf_set_string(sf, SF_STR_COPYRIGHT, song.copyright.c_str());

  // 7. COMMENTS / NOTES
  if (!song.notes.empty()) sf_set_string(sf, SF_STR_COMMENT, song.notes.c_str());

  // 8. SOFTWARE STAMP
  sf_set_string(sf, SF_STR_SOFTWARE, "Furnace Tracker");
}

void DivEngine::runExportThread() {
  size_t fadeOutSamples=got.rate*exportFadeOut;
  size_t curFadeOutSample=0;
  isFadingOut=false;

  switch (exportMode) {
    case DIV_EXPORT_MODE_ONE: {
      SNDFILE* sf;
      SF_INFO si;
      SFWrapper sfWrap;
      memset(&si,0,sizeof(SF_INFO));
      si.samplerate=got.rate;
      si.channels=exportOutputs;
      switch (exportFormat) {
        case DIV_EXPORT_FORMAT_WAV:
          si.format=SF_FORMAT_WAV;
          switch (wavFormat) {
            case DIV_EXPORT_WAV_U8:
              si.format|=SF_FORMAT_PCM_U8;
              break;
            case DIV_EXPORT_WAV_S16:
              si.format|=SF_FORMAT_PCM_16;
              break;
            case DIV_EXPORT_WAV_F32:
              si.format|=SF_FORMAT_FLOAT;
              break;
            default:
              si.format|=SF_FORMAT_PCM_U8;
              break;
          }
          break;
        case DIV_EXPORT_FORMAT_OPUS:
          si.format=SF_FORMAT_OGG|SF_FORMAT_OPUS;
          break;
        case DIV_EXPORT_FORMAT_FLAC:
          si.format=SF_FORMAT_FLAC|SF_FORMAT_PCM_16;
          break;
        case DIV_EXPORT_FORMAT_VORBIS:
          si.format=SF_FORMAT_OGG|SF_FORMAT_VORBIS;
          break;
        case DIV_EXPORT_FORMAT_MPEG_L3:
          si.format=SF_FORMAT_MPEG|SF_FORMAT_MPEG_LAYER_III;
          break;
      }

      sf=sfWrap.doOpen(exportPath.c_str(),SFM_WRITE,&si);
      if (sf==NULL) {
        logE("could not open file for writing! (%s)",sf_strerror(NULL));
        exporting=false;
        return;
      }

      applyExportMetadata(sf,song,exportMode,0);

      MAP_BITRATE;

      float* outBuf[DIV_MAX_OUTPUTS];
      float* outBufFinal;
      for (int i=0; i<exportOutputs; i++) {
        outBuf[i]=new float[EXPORT_BUFSIZE];
      }
      outBufFinal=new float[EXPORT_BUFSIZE*exportOutputs];

      // take control of audio output
      deinitAudioBackend();
      freelance=false;
      playSub(false);
      freelance=false;

      logI("rendering to file...");

      while (playing) {
        size_t total=0;
        nextBuf(NULL,outBuf,0,exportOutputs,EXPORT_BUFSIZE);
        if (totalProcessed>EXPORT_BUFSIZE) {
          logE("error: total processed is bigger than export bufsize! %d>%d",totalProcessed,EXPORT_BUFSIZE);
          totalProcessed=EXPORT_BUFSIZE;
        }
        int fi=0;
        for (int i=0; i<(int)totalProcessed; i++) {
          total++;
          if (isFadingOut) {
            double mul=(1.0-((double)curFadeOutSample/(double)fadeOutSamples));
            if (fadeOutSamples<1.0) mul=0.0;
            for (int j=0; j<exportOutputs; j++) {
              outBufFinal[fi++]=MAX(-1.0f,MIN(1.0f,outBuf[j][i]))*mul;
            }
            if (++curFadeOutSample>=fadeOutSamples) {
              playing=false;
              break;
            }
          } else {
            for (int j=0; j<exportOutputs; j++) {
              outBufFinal[fi++]=MAX(-1.0f,MIN(1.0f,outBuf[j][i]));
            }
            if (lastLoopPos>-1 && i>=lastLoopPos && totalLoops>=exportLoopCount) {
              logD("start fading out...");
              isFadingOut=true;
              if (fadeOutSamples==0) break;
            }
          }
        }
        
        if (sf_writef_float(sf,outBufFinal,total)!=(int)total) {
          logE("error: failed to write entire buffer!");
          break;
        }
      }

      delete[] outBufFinal;
      for (int i=0; i<exportOutputs; i++) {
        delete[] outBuf[i];
      }

      if (sfWrap.doClose()!=0) {
        logE("could not close audio file!");
      }

      if (initAudioBackend()) {
        for (int i=0; i<song.systemLen; i++) {
          disCont[i].setRates(got.rate);
          disCont[i].setQuality(lowQuality,dcHiPass);
        }
        if (!output->setRun(true)) {
          logE("error while activating audio!");
        }
      }
      logI("done!");
      exporting=false;
      break;
    }
    case DIV_EXPORT_MODE_MANY_SYS: {
      SNDFILE* sf[DIV_MAX_CHIPS];
      SF_INFO si[DIV_MAX_CHIPS];
      String fname[DIV_MAX_CHIPS];
      SFWrapper sfWrap[DIV_MAX_CHIPS];
      for (int i=0; i<song.systemLen; i++) {
        memset(&si[i],0,sizeof(SF_INFO));
        sf[i]=NULL;
        si[i].samplerate=got.rate;
        si[i].channels=disCont[i].dispatch->getOutputCount();
        si[i].format=SF_FORMAT_WAV|SF_FORMAT_PCM_16;
      }

      const char* ext = ".wav";
      switch (exportFormat) {
        case DIV_EXPORT_FORMAT_FLAC:
          ext = ".flac";
          break;
        case DIV_EXPORT_FORMAT_OPUS:
        case DIV_EXPORT_FORMAT_VORBIS:
          ext = ".ogg";
          break;
        case DIV_EXPORT_FORMAT_MPEG_L3:
          ext = ".mp3";
          break;
        default:
          ext = ".wav";
          break;
      }

      for (int i=0; i<song.systemLen; i++) {
        fname[i]=fmt::sprintf("%s_s%02d%s",exportPath.c_str(),i+1,ext);
        logI("- %s",fname[i].c_str());
        sf[i]=sfWrap[i].doOpen(fname[i].c_str(),SFM_WRITE,&si[i]);
        if (sf[i]==NULL) {
          logE("could not open file for writing! (%s)",sf_strerror(NULL));
          for (int j=0; j<i; j++) {
            sfWrap[i].doClose();
          }
          return;
        }
        applyExportMetadata(sf[i],song,exportMode,i);
      }

      float* outBuf[DIV_MAX_OUTPUTS];
      memset(outBuf,0,sizeof(void*)*DIV_MAX_OUTPUTS);
      outBuf[0]=new float[EXPORT_BUFSIZE];
      outBuf[1]=new float[EXPORT_BUFSIZE];
      short* sysBuf[DIV_MAX_CHIPS];
      for (int i=0; i<song.systemLen; i++) {
        sysBuf[i]=new short[EXPORT_BUFSIZE*disCont[i].dispatch->getOutputCount()];
      }

      // take control of audio output
      deinitAudioBackend();
      freelance=false;
      playSub(false);
      freelance=false;

      logI("rendering to files...");

      while (playing) {
        size_t total=0;
        nextBuf(NULL,outBuf,0,2,EXPORT_BUFSIZE);
        if (totalProcessed>EXPORT_BUFSIZE) {
          logE("error: total processed is bigger than export bufsize! %d>%d",totalProcessed,EXPORT_BUFSIZE);
          totalProcessed=EXPORT_BUFSIZE;
        }
        for (int j=0; j<(int)totalProcessed; j++) {
          total++;
          if (isFadingOut) {
            double mul=(1.0-((double)curFadeOutSample/(double)fadeOutSamples));
            if (fadeOutSamples<1.0) mul=0.0;
            for (int i=0; i<song.systemLen; i++) {
              for (int k=0; k<si[i].channels; k++) {
                if (disCont[i].bbOut[k]==NULL) {
                  sysBuf[i][k+(j*si[i].channels)]=0;
                } else {
                  sysBuf[i][k+(j*si[i].channels)]=(double)disCont[i].bbOut[k][j]*mul;
                }
              }
            }
            if (++curFadeOutSample>=fadeOutSamples) {
              playing=false;
              break;
            }
          } else {
            for (int i=0; i<song.systemLen; i++) {
              for (int k=0; k<si[i].channels; k++) {
                if (disCont[i].bbOut[k]==NULL) {
                  sysBuf[i][k+(j*si[i].channels)]=0;
                } else {
                  sysBuf[i][k+(j*si[i].channels)]=disCont[i].bbOut[k][j];
                }
              }
            }
            if (lastLoopPos>-1 && j>=lastLoopPos && totalLoops>=exportLoopCount) {
              logD("start fading out...");
              isFadingOut=true;
              if (fadeOutSamples==0) break;
            }
          }
        }
        for (int i=0; i<song.systemLen; i++) {
          if (sf_writef_short(sf[i],sysBuf[i],total)!=(int)total) {
            logE("error: failed to write entire buffer! (%d)",i);
            break;
          }
        }
      }

      delete[] outBuf[0];
      delete[] outBuf[1];

      for (int i=0; i<song.systemLen; i++) {
        delete[] sysBuf[i];
        if (sfWrap[i].doClose()!=0) {
          logE("could not close audio file!");
        }
      }

      if (initAudioBackend()) {
        for (int i=0; i<song.systemLen; i++) {
          disCont[i].setRates(got.rate);
          disCont[i].setQuality(lowQuality,dcHiPass);
        }
        if (!output->setRun(true)) {
          logE("error while activating audio!");
        }
      }
      logI("done!");
      exporting=false;
      break;
    }
    case DIV_EXPORT_MODE_MANY_CHAN: {
      // take control of audio output
      deinitAudioBackend();

      curExportChan=0;

      float* outBuf[DIV_MAX_OUTPUTS];
      float* outBufFinal;
      for (int i=0; i<exportOutputs; i++) {
        outBuf[i]=new float[EXPORT_BUFSIZE];
      }
      outBufFinal=new float[EXPORT_BUFSIZE*exportOutputs];

      logI("rendering to files...");

      const char* ext = ".wav";
      switch (exportFormat) {
        case DIV_EXPORT_FORMAT_FLAC:
          ext = ".flac";
          break;
        case DIV_EXPORT_FORMAT_OPUS:
        case DIV_EXPORT_FORMAT_VORBIS:
          ext = ".ogg";
          break;
        case DIV_EXPORT_FORMAT_MPEG_L3:
          ext = ".mp3";
          break;
        default:
          ext = ".wav";
          break;
      }
      
      for (int i=0; i<song.chans; i++) {
        if (!exportChannelMask[i]) continue;

        SNDFILE* sf;
        SF_INFO si;
        SFWrapper sfWrap;
        memset(&si,0,sizeof(SF_INFO));
        String fname = fmt::sprintf("%s_c%02d%s",exportPath.c_str(),i+1,ext);
        logI("- %s",fname.c_str());
        si.samplerate=got.rate;
        si.channels=exportOutputs;
        switch (exportFormat) {
          case DIV_EXPORT_FORMAT_WAV:
            si.format=SF_FORMAT_WAV;
            switch (wavFormat) {
              case DIV_EXPORT_WAV_U8:
                si.format|=SF_FORMAT_PCM_U8;
                break;
              case DIV_EXPORT_WAV_S16:
                si.format|=SF_FORMAT_PCM_16;
                break;
              case DIV_EXPORT_WAV_F32:
                si.format|=SF_FORMAT_FLOAT;
                break;
              default:
                si.format|=SF_FORMAT_PCM_U8;
                break;
            }
            break;
          case DIV_EXPORT_FORMAT_OPUS:
            si.format=SF_FORMAT_OGG|SF_FORMAT_OPUS;
            break;
          case DIV_EXPORT_FORMAT_FLAC:
            si.format=SF_FORMAT_FLAC|SF_FORMAT_PCM_16;
            break;
          case DIV_EXPORT_FORMAT_VORBIS:
            si.format=SF_FORMAT_OGG|SF_FORMAT_VORBIS;
            break;
          case DIV_EXPORT_FORMAT_MPEG_L3:
            si.format=SF_FORMAT_MPEG|SF_FORMAT_MPEG_LAYER_III;
            break;
        }

        sf=sfWrap.doOpen(fname.c_str(),SFM_WRITE,&si);
        if (sf==NULL) {
          logE("could not open file for writing! (%s)",sf_strerror(NULL));
          break;
        }

        applyExportMetadata(sf,song,exportMode,i);

        MAP_BITRATE;

        for (int j=0; j<song.chans; j++) {
          bool mute=(j!=i);
          isMuted[j]=mute;
        }
        if (getChannelType(i)==5) {
          for (int j=i; j<song.chans; j++) {
            if (getChannelType(j)!=5) break;
            isMuted[j]=false;
          }
        }
        for (int j=0; j<song.chans; j++) {
          if (disCont[song.dispatchOfChan[j]].dispatch!=NULL && song.dispatchChanOfChan[j]>=0) {
            disCont[song.dispatchOfChan[j]].dispatch->muteChannel(song.dispatchChanOfChan[j],isMuted[j]);
          }
        }
        
        curOrder=0;
        prevOrder=0;
        curFadeOutSample=0;
        lastLoopPos=-1;
        totalLoops=0;
        isFadingOut=false;
        remainingLoops=-1;
        freelance=false;
        playSub(false);
        freelance=false;

        while (playing) {
          size_t total=0;
          nextBuf(NULL,outBuf,0,exportOutputs,EXPORT_BUFSIZE);
          if (totalProcessed>EXPORT_BUFSIZE) {
            logE("error: total processed is bigger than export bufsize! %d>%d",totalProcessed,EXPORT_BUFSIZE);
            totalProcessed=EXPORT_BUFSIZE;
          }
          int fi=0;
          for (int j=0; j<(int)totalProcessed; j++) {
            total++;
            if (isFadingOut) {
              double mul=(1.0-((double)curFadeOutSample/(double)fadeOutSamples));
              if (fadeOutSamples<1.0) mul=0.0;
              for (int k=0; k<exportOutputs; k++) {
                outBufFinal[fi++]=MAX(-1.0f,MIN(1.0f,outBuf[k][j]))*mul;
              }
              if (++curFadeOutSample>=fadeOutSamples) {
                playing=false;
                break;
              }
            } else {
              for (int k=0; k<exportOutputs; k++) {
                outBufFinal[fi++]=MAX(-1.0f,MIN(1.0f,outBuf[k][j]));
              }
              if (lastLoopPos>-1 && j>=lastLoopPos && totalLoops>=exportLoopCount) {
                logD("start fading out...");
                isFadingOut=true;
                if (fadeOutSamples==0) break;
              }
            }
          }
          if (sf_writef_float(sf,outBufFinal,total)!=(int)total) {
            logE("error: failed to write entire buffer!");
            break;
          }
        }

        curExportChan++;

        if (sfWrap.doClose()!=0) {
          logE("could not close audio file!");
        }

        if (getChannelType(i)==5) {
          i++;
          while (true) {
            if (i>=song.chans) break;
            if (getChannelType(i)!=5) break;
            i++;
          }
          i--;
        }

        if (stopExport) break;
      }

      delete[] outBufFinal;
      for (int i=0; i<exportOutputs; i++) {
        delete[] outBuf[i];
      }

      for (int i=0; i<song.chans; i++) {
        isMuted[i]=false;
        if (disCont[song.dispatchOfChan[i]].dispatch!=NULL && song.dispatchChanOfChan[i]>=0) {
          disCont[song.dispatchOfChan[i]].dispatch->muteChannel(song.dispatchChanOfChan[i],false);
        }
      }

      if (initAudioBackend()) {
        for (int i=0; i<song.systemLen; i++) {
          disCont[i].setRates(got.rate);
          disCont[i].setQuality(lowQuality,dcHiPass);
        }
        if (!output->setRun(true)) {
          logE("error while activating audio!");
        }
      }
      logI("done!");
      exporting=false;
      curExportChan=0;
      break;
    }
  }

  stopExport=false;
}
#else
void DivEngine::runExportThread() {
}
#endif

bool DivEngine::shallSwitchCores() {
  return true;
}

bool DivEngine::saveAudio(const char* path, DivAudioExportOptions options) {
#ifndef HAVE_SNDFILE
  logE("Furnace was not compiled with libsndfile. cannot export!");
  return false;
#else
  exportPath=path;
  exportMode=options.mode;
  exportFormat=options.format;
  wavFormat=options.wavFormat;
  exportBitRate=options.bitRate;
  exportBitRateMode=options.bitRateMode;
  exportVBRQuality=options.vbrQuality;
  exportFadeOut=options.fadeOut;
  memcpy(exportChannelMask,options.channelMask,DIV_MAX_CHANS*sizeof(bool));
  if (exportMode!=DIV_EXPORT_MODE_ONE) {
    // before, it only checked for .wav
    size_t dotPos = exportPath.rfind('.');
    size_t slashPos = exportPath.find_last_of("/\\");

    if (dotPos != String::npos && (slashPos == String::npos || dotPos > slashPos)) {
      exportPath = exportPath.substr(0, dotPos);
    }
  }
  exporting=true;
  stopExport=false;
  stop();
  repeatPattern=false;
  setOrder(0);
  remainingLoops=-1;
  if (options.format==DIV_EXPORT_FORMAT_OPUS) {
    // Opus only supports 48KHz and a couple divisors of that number...
    got.rate=48000;
  } else {
    got.rate=options.sampleRate;
  }

  if (shallSwitchCores()) {
    bool isMutedBefore[DIV_MAX_CHANS];
    memcpy(isMutedBefore,isMuted,DIV_MAX_CHANS*sizeof(bool));
    quitDispatch();
    initDispatch(true);
    renderSamplesP();
    for (int i=0; i<song.chans; i++) {
      if (isMutedBefore[i]) {
        muteChannel(i,true);
      }
    }
  }

  exportOutputs=options.chans;
  if (exportOutputs<1) exportOutputs=1;
  if (exportOutputs>DIV_MAX_OUTPUTS) exportOutputs=DIV_MAX_OUTPUTS;

  exportLoopCount=options.loops+1;
  exportThread=new std::thread(_runExportThread,this);
  return true;
#endif
}

void DivEngine::waitAudioFile() {
  if (exportThread!=NULL) {
    exportThread->join();
  }
}

bool DivEngine::haltAudioFile() {
  stopExport=true;
  stop();
  waitAudioFile();
  finishAudioFile();
  return true;
}

void DivEngine::finishAudioFile() {
  if (shallSwitchCores()) {
    bool isMutedBefore[DIV_MAX_CHANS];
    memcpy(isMutedBefore,isMuted,DIV_MAX_CHANS*sizeof(bool));
    quitDispatch();
    initDispatch(false);
    renderSamplesP();
    for (int i=0; i<song.chans; i++) {
      if (isMutedBefore[i]) {
        muteChannel(i,true);
      }
    }
  }
}

