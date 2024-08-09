#include "mister.h"
#include "../retroarch.h"

mister_video_t mister_video;

void mister_CmdClose(void)
{
   if (!mister_video.isConnected)
      return;

   char buffer[1];
   buffer[0] = mister_CMD_CLOSE;
   mister_Send((char*)&buffer[0], 1);

#ifdef WIN32
   closesocket(mister_video.sockfd);
   WSACleanup();
#else
   close(mister_video.sockfd);
#endif
}

void mister_CmdInit(const char* mister_host, short mister_port, bool lz4_frames,uint32_t sound_rate, uint8_t sound_chan)
{
   if (mister_video.isConnected)
      return;

   char buffer[4];

   sr_init();

   // to do better find ini files
   const char *_core_name = (const char*)runloop_state_get_ptr()->system.info.library_name;
   char ini_file[160];
   strcpy(ini_file, "config/");
   strcat(ini_file, _core_name);
   strcat(ini_file, "/");
   strcat(ini_file, _core_name);
   strcat(ini_file, ".switchres.ini");
   RARCH_LOG("[MiSTer] Core switchres configuration on %s...\n", ini_file);
   sr_load_ini(ini_file);

#ifdef _WIN32

   WSADATA wsd;
   uint16_t rc;

   RARCH_LOG("[MiSTer] Initialising Winsock...\n");
   // Load Winsock
   rc = WSAStartup(MAKEWORD(2, 2), &wsd);
   if (rc != 0)
   {
      RARCH_WARN("Unable to load Winsock: %d\n", rc);
   }
   RARCH_LOG("[MiSTer] Initialising socket...\n");
   mister_video.sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
   if (mister_video.sockfd < 0)
   {
      RARCH_WARN("socket error\n");
   }

   memset(&mister_video.ServerAddr, 0, sizeof(mister_video.ServerAddr));
   mister_video.ServerAddr.sin_family = AF_INET;
   mister_video.ServerAddr.sin_port = htons(mister_port);
   mister_video.ServerAddr.sin_addr.s_addr = inet_addr(mister_host);

   RARCH_LOG("[MiSTer] Setting socket async...\n");
   u_long iMode=1;
   rc = ioctlsocket(mister_video.sockfd, FIONBIO, &iMode);
   if (rc < 0)
   {
      RARCH_WARN("set nonblock fail\n");
   }

   RARCH_LOG("[MiSTer] Setting send buffer to 2097152 bytes...\n");
   int optVal = 2097152;
   int optLen = sizeof(int);
   rc = setsockopt(mister_video.sockfd, SOL_SOCKET, SO_SNDBUF, (char*)&optVal, optLen);
   if (rc < 0)
   {
      RARCH_WARN("set so_sndbuff fail\n");
   }

#else

   RARCH_LOG("[MiSTer] Initialising socket...\n");
   mister_video.sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
   if (mister_video.sockfd < 0)
   {
      RARCH_WARN("socket error\n");
   }

   memset(&mister_video.ServerAddr, 0, sizeof(mister_video.ServerAddr));
   mister_video.ServerAddr.sin_family = AF_INET;
   mister_video.ServerAddr.sin_port = htons(mister_port);
   mister_video.ServerAddr.sin_addr.s_addr = inet_addr(mister_host);

   RARCH_LOG("[MiSTer] Setting socket async...\n");
   // Non blocking socket
   int flags;
   flags = fcntl(mister_video.sockfd, F_GETFD, 0);
   if (flags < 0)
   {
      RARCH_WARN("get falg error\n");
   }
   flags |= O_NONBLOCK;
   if (fcntl(mister_video.sockfd, F_SETFL, flags) < 0)
   {
      RARCH_WARN("set nonblock fail\n");
   }

   RARCH_LOG("[MiSTer] Setting send buffer to 2097152 bytes...\n");
   // Settings
   int size = 2 * 1024 * 1024;
   if (setsockopt(mister_video.sockfd, SOL_SOCKET, SO_SNDBUF, (void*)&size, sizeof(size)) < 0)
   {
      RARCH_WARN("Error so_sndbuff\n");
   }

#endif

   RARCH_LOG("[MiSTer] Sending CMD_INIT...lz4 %d sound_rate %d sound_chan %d\n", lz4_frames, sound_rate, sound_chan);

   buffer[0] = mister_CMD_INIT;
   buffer[1] = (lz4_frames) ? 1 : 0; //0-RAW or 1-LZ4 ;
   buffer[2] = (sound_rate == 22050) ? 1 : (sound_rate == 44100) ? 2 : (sound_rate == 48000) ? 3 : 0;
   buffer[3] = sound_chan;


   mister_Send(&buffer[0], 4);

   mister_video.lz4_compress = lz4_frames;
   mister_video.width = 0;
   mister_video.height = 0;
   mister_video.width_core = 0;
   mister_video.height_core = 0;
   mister_video.lines = 0;
   mister_video.lines_padding = 0;
   mister_video.vfreq = 0;
   mister_video.vfreq_core = 0;
   mister_video.widthTime = 0;
   mister_video.frameTime = 0;
   mister_video.avgEmulationTime = 0;
   mister_video.vsync_auto = 120;
   mister_video.avgBlitTime = 0;
   mister_video.frameEcho = 0;
   mister_video.vcountEcho = 0;
   mister_video.frameGPU = 0;
   mister_video.vcountGPU = 0;
   mister_video.interlaced = 0;
   mister_video.fpga_audio = 0;

   mister_video.isConnected = true;
}

void mister_CmdSwitchres(int w, int h, double vfreq, int orientation)
{
   if (!mister_video.isConnected)
      return;

   if (w < 200 || h < 160)
      return;

   if (w == mister_video.width_core && h == mister_video.height_core && vfreq == mister_video.vfreq_core)
      return;

   mister_video.width_core = w;
   mister_video.height_core = h;
   mister_video.vfreq_core = vfreq;

   unsigned char retSR;
   sr_mode swres_result;
   int sr_mode_flags = 0;

   if (h > 288)
      sr_mode_flags = SR_MODE_INTERLACED;

   if (orientation)
      sr_mode_flags = sr_mode_flags | SR_MODE_ROTATED;

   RARCH_LOG("[MiSTer] Video_SetSwitchres - (in %dx%d@%f)\n", w, h, vfreq);

   if (h < 288 && !mister_is15()) h = h << 1; //fix width
   if (h > 288 && h <= 400 && mister_is15()) h = 480; //fix height dosbox-pure

   retSR = sr_add_mode(w, h, vfreq, sr_mode_flags, &swres_result);
   if (retSR)
   {
      RARCH_LOG("[MiSTer] Video_SetSwitchres - (result %dx%d@%f) - x=%.4f y=%.4f stretched(%d)\n", swres_result.width, swres_result.height,swres_result.vfreq, swres_result.x_scale, swres_result.y_scale, swres_result.is_stretched);
   }

   if (retSR && (swres_result.width != mister_video.width || swres_result.height != mister_video.height || mister_video.vfreq != swres_result.vfreq))
   {
      char buffer[26];

      double px = (double) swres_result.pclock / 1000000.0;
      uint16_t udp_hactive = swres_result.width;
      uint16_t udp_hbegin = swres_result.hbegin;
      uint16_t udp_hend = swres_result.hend;
      uint16_t udp_htotal = swres_result.htotal;
      uint16_t udp_vactive = swres_result.height;
      uint16_t udp_vbegin = swres_result.vbegin;
      uint16_t udp_vend = swres_result.vend;
      uint16_t udp_vtotal = swres_result.vtotal;
      uint8_t  udp_interlace = swres_result.interlace;

      mister_video.width = udp_hactive;
      mister_video.height = udp_vactive;
      mister_video.lines_padding = (udp_vactive > h && udp_vactive != h * 2) ? udp_vactive - h : 0;
      mister_video.vfreq = vfreq;
      mister_video.lines = udp_vtotal;
      mister_video.interlaced = udp_interlace;
      mister_video.downscaled = 0;

      mister_video.widthTime = round((double) udp_htotal * (1 / px)); //in usec, time to raster 1 line
      mister_video.frameTime = mister_video.widthTime * udp_vtotal;

      if (mister_video.interlaced)
      {
         mister_video.frameField = 0;
         mister_video.frameTime = mister_video.frameTime >> 1;
      }

      if (h > mister_video.height)
      {
         mister_video.downscaled = 1;
      }

      RARCH_LOG("[MiSTer] Sending CMD_SWITCHRES...\n");
      buffer[0] = mister_CMD_SWITCHRES;
      memcpy(&buffer[1],&px,sizeof(px));
      memcpy(&buffer[9],&udp_hactive,sizeof(udp_hactive));
      memcpy(&buffer[11],&udp_hbegin,sizeof(udp_hbegin));
      memcpy(&buffer[13],&udp_hend,sizeof(udp_hend));
      memcpy(&buffer[15],&udp_htotal,sizeof(udp_htotal));
      memcpy(&buffer[17],&udp_vactive,sizeof(udp_vactive));
      memcpy(&buffer[19],&udp_vbegin,sizeof(udp_vbegin));
      memcpy(&buffer[21],&udp_vend,sizeof(udp_vend));
      memcpy(&buffer[23],&udp_vtotal,sizeof(udp_vtotal));
      memcpy(&buffer[25],&udp_interlace,sizeof(udp_interlace));
      mister_Send(&buffer[0], 26);
   }
}

void mister_CmdBlit(char *bufferFrame, uint16_t vsync)
{
   if (!mister_video.isConnected)
      return;

   char buffer[9];

   mister_video.frame++;

   // 16 or 32 lines blockSize
   uint8_t blockLinesFactor = (mister_video.width > 384) ? 5   : 4;

   // Compressed blocks are 16/32 lines long
   uint32_t blockSize = (mister_video.lz4_compress) ? (mister_video.width << blockLinesFactor) * 3 : 0;

   if (blockSize > mister_MAX_LZ4_BLOCK)
      blockSize = mister_MAX_LZ4_BLOCK;

   // Manual vsync
   if (vsync != 0)
   {
      mister_video.vsync_auto = vsync;
   }

   buffer[0] = mister_CMD_BLIT_VSYNC;
   memcpy(&buffer[1], &mister_video.frame, sizeof(mister_video.frame));
   memcpy(&buffer[5], &mister_video.vsync_auto, sizeof(mister_video.vsync_auto));
   buffer[7] = (uint16_t) blockSize & 0xff;
   buffer[8] = (uint16_t) blockSize >> 8;

   mister_Send(&buffer[0], 9);

   uint32_t bufferSize = (mister_video.interlaced == 0) ? mister_video.width * mister_video.height * 3 : mister_video.width * (mister_video.height >> 1) * 3;

   bufferSize += (mister_video.width * mister_video.lines_padding * 3) >>  mister_video.interlaced; //hack when switchres and core resolution not match

   if (mister_video.lz4_compress == false)
      mister_SendMTU(&bufferFrame[0], bufferSize, 1470);
   else
      mister_SendLZ4(&bufferFrame[0], bufferSize, blockSize);
}

void mister_CmdAudio(const void *bufferFrame, uint32_t sizeSound, uint8_t soundchan)
{
   if (!mister_video.isConnected)
      return;

   if (!mister_video.fpga_audio)
      return;

   char buffer[3];
   buffer[0] = mister_CMD_AUDIO;

   uint16_t bytesSound = sizeSound * soundchan * 2;

   memcpy(&buffer[1], &bytesSound, sizeof(bytesSound));

   mister_Send(&buffer[0], 3);
   const uint8_t *data_in = (const uint8_t *)bufferFrame;

   mister_SendMTU((char *) &data_in[0], bytesSound, 1472);
}

void mister_setBlitTime(retro_time_t blitTime)
{
   if (mister_video.frame > 10) //first frames spends more time as usual
   {
      mister_video.avgBlitTime = (mister_video.avgBlitTime == 0) ? blitTime: (mister_video.avgBlitTime + blitTime) / 2;
   }
   else
   {
      mister_video.avgBlitTime = 0;
   }
}

int mister_Sync(retro_time_t emulationTime)
{
   if (!mister_video.isConnected)
      return 0;

   if (mister_video.frame > 10) //first frames spends more time as usual
   {
      mister_video.avgEmulationTime = (mister_video.avgEmulationTime == 0) ? emulationTime + mister_video.avgBlitTime: (mister_video.avgEmulationTime + emulationTime + mister_video.avgBlitTime) / 2;
      mister_video.vsync_auto = mister_video.height - round(mister_video.lines * mister_video.avgEmulationTime) / mister_video.frameTime; //vblank for desviation
      if (mister_video.vsync_auto > 480) mister_video.vsync_auto = 1;
   }
   else
   {
      mister_video.avgEmulationTime = 0;
      mister_video.vsync_auto = 120;
   }

   int diffTime = mister_GetVSyncDif();  //adjusting time with raster
   if (diffTime > 60000 || diffTime < -60000)
      return 0;
   else
      return diffTime;
}

int mister_GetVSyncDif(void)
{
   uint32_t prevFrameEcho = mister_video.frameEcho;
   int diffTime = 0;

   if (mister_video.frame != mister_video.frameEcho) //some ack is pending
   {
      mister_ReceiveBlitACK();
   }

   if (prevFrameEcho != mister_video.frameEcho) //if ack is updated, check raster difference
   {
      //horror patch if emulator freezes to align frame counter
      if ((mister_video.frameEcho + 1) < mister_video.frameGPU)
      {
         mister_video.frame = mister_video.frameGPU + 1;
      }

      uint32_t vcount1 = ((mister_video.frameEcho - 1) * mister_video.lines + mister_video.vcountEcho) >> mister_video.interlaced;
      uint32_t vcount2 = (mister_video.frameGPU * mister_video.lines + mister_video.vcountGPU) >> mister_video.interlaced;
      int dif = (int)(vcount1 - vcount2) / 2;   //dicotomic

      diffTime = (int)(mister_video.widthTime * dif);

      //printf("echo %d %d / %d %d (dif (vc1=%d,vc2=%d) %d, diffTime=%d)\n", mister_video.frameEcho, mister_video.vcountEcho, mister_video.frameGPU, mister_video.vcountGPU,vcount1,vcount2,dif,diffTime);
   }

   return diffTime;
}


bool mister_is15(void)
{
   sr_state swres_state;
   sr_get_state(&swres_state);

   return (!strcmp("arcade_15",swres_state.monitor) || !strcmp("generic_15",swres_state.monitor) || !strcmp("ntsc",swres_state.monitor) || !strcmp("pal",swres_state.monitor) || !strcmp("arcade_15_25",swres_state.monitor) || !strcmp("arcade_15_25_31",swres_state.monitor));
}

bool mister_isInterlaced(void)
{
   return mister_video.interlaced;
}

bool mister_is480p(void)
{
   return (!mister_video.interlaced && (mister_video.height > 288 || mister_video.height_core == mister_video.height >> 1));
}

bool mister_isDownscaled(void)
{
   return mister_video.downscaled;
}

int mister_GetField(void)
{
   int field = 0;
   if (mister_video.interlaced)
   {
      mister_video.frameField = !mister_video.frameField;
      field = mister_video.frameField;
   }

   return field;
}

uint16_t mister_GetWidth(void)
{
   return mister_video.width;
}

uint16_t mister_GetHeight(void)
{
   return mister_video.height;
}
//Private
void mister_Send(void *cmd, int cmdSize)
{
   sendto(mister_video.sockfd, (char *) cmd, cmdSize, 0, (struct sockaddr *)&mister_video.ServerAddr, sizeof(mister_video.ServerAddr));
}


void mister_SendMTU(char *buffer, int bytes_to_send, int chunk_max_size)
{
   int bytes_this_chunk = 0;
   int chunk_size = 0;
   uint32_t offset = 0;

   do
   {
      chunk_size = bytes_to_send > chunk_max_size? chunk_max_size : bytes_to_send;
      bytes_to_send -= chunk_size;
      bytes_this_chunk = chunk_size;

      mister_Send(buffer + offset, bytes_this_chunk);
      offset += chunk_size;

   } while (bytes_to_send > 0);
}

void mister_SendLZ4(char *buffer, int bytes_to_send, int block_size)
{
   LZ4_stream_t lz4_stream_body;
   LZ4_stream_t* lz4_stream = &lz4_stream_body;
   LZ4_initStream(lz4_stream, sizeof(*lz4_stream));
/*
   LZ4_streamHC_t lz4_streamHC_body;
   LZ4_streamHC_t* lz4_streamHC = &lz4_streamHC_body;
   LZ4_initStreamHC(lz4_streamHC, sizeof(*lz4_streamHC));
*/
   int inp_buf_index = 0;
   int bytes_this_chunk = 0;
   int chunk_size = 0;
   uint32_t offset = 0;

   do
   {
      chunk_size = bytes_to_send > block_size? block_size : bytes_to_send;
      bytes_to_send -= chunk_size;
      bytes_this_chunk = chunk_size;

      char* const inp_ptr = mister_video.inp_buf[inp_buf_index];
      memcpy((char *)&inp_ptr[0], buffer + offset, chunk_size);

      const uint16_t c_size = LZ4_compress_fast_continue(lz4_stream, inp_ptr, (char *)&mister_video.m_fb_compressed[2], bytes_this_chunk, sizeof(mister_video.m_fb_compressed), 1);
      //const uint16_t c_size = LZ4_compress_HC_continue(lz4_streamHC, inp_ptr, (char *)&m_fb_compressed[2], bytes_this_chunk, sizeof(m_fb_compressed));

      uint16_t *c_size_ptr = (uint16_t *)&mister_video.m_fb_compressed[0];
      *c_size_ptr = c_size;

      mister_SendMTU((char *) &mister_video.m_fb_compressed[0], c_size + 2, 1472);
      offset += chunk_size;
      inp_buf_index ^= 1;

   } while (bytes_to_send > 0);
}

void mister_ReceiveBlitACK(void)
{
   uint32_t frameUDP = mister_video.frameEcho;
   socklen_t sServerAddr = sizeof(struct sockaddr);
   int len = 0;
   do
   {
      len = recvfrom(mister_video.sockfd, mister_video.bufferRecv, sizeof(mister_video.bufferRecv), 0, (struct sockaddr *)&mister_video.ServerAddr, &sServerAddr);
      if (len > 0)
      {
         memcpy(&frameUDP, &mister_video.bufferRecv[0],4);
         if (frameUDP > mister_video.frameEcho)
         {
            mister_video.frameEcho = frameUDP;
            memcpy(&mister_video.vcountEcho, &mister_video.bufferRecv[4],2);
            memcpy(&mister_video.frameGPU, &mister_video.bufferRecv[6],4);
            memcpy(&mister_video.vcountGPU, &mister_video.bufferRecv[10],2);
            memcpy(&mister_video.fpga_debug_bits, &mister_video.bufferRecv[12],1);

            bitByte bits;
            bits.byte = mister_video.fpga_debug_bits;
            mister_video.fpga_vram_ready     = bits.u.bit0;
            mister_video.fpga_vram_end_frame = bits.u.bit1;
            mister_video.fpga_vram_synced    = bits.u.bit2;
            mister_video.fpga_vga_frameskip  = bits.u.bit3;
            mister_video.fpga_vga_vblank     = bits.u.bit4;
            mister_video.fpga_vga_f1         = bits.u.bit5;
            mister_video.fpga_audio          = bits.u.bit6;
            mister_video.fpga_vram_queue     = bits.u.bit7;
            //printf("ReceiveBlitACK %d %d / %d %d \n", frameEcho, vcountEcho, frameGPU, vcountGPU);
         }
      }
   } while (len > 0 && mister_video.frame != mister_video.frameEcho);
}
