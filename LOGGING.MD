  //
  //  LOGGING  LOGGING  LOGGING  LOGGING  LOGGING  LOGGING  LOGGING  LOGGING  LOGGING  LOGGING  LOGGING  
  //
  //  There are 3 logging "channels" used during system boot:
  //    A) Logging to the file EBTKSLog.log the SDCARD root directory.       Look for calls to LOGPRINTF()
  //    B) Logging to the HP8x CRT.                                          Look for usage of log_to_CRT_ptr which is almost entirely in loadConfiguration()
  //    C) Logging to the diagnostic serial port (over USB).                 Look for usage of log_to_serial_ptr which is in this file and loadConfiguration()
  //
  //  (A) Depends on a functioning SD Card interface and an SD Card.
  //  (B) Depends on the HP8x being in run mode and able to do DMA. Until that occurs (when the boot process has completed), we buffer these messages
  //      in CRT_Log_Buffer, which has limited size. We DO NOT check for overflow of this buffer, but we do send a message to (A) and (C) indicating
  //      the amount of the buffer used. Look for something like this "CRT Log buffer usage is 685 of 1024 charcters"
  //  (C) Like (B), we hold off sending these messages to the USB serial port, except the wait is until we get to loop().
  //      The buffer also has a limited size. We DO NOT check for overflow of this buffer, but we do send a message to (A) and (C) indicating
  //      the amount of the buffer used. Look for something like this "Serial Log buffer usage is 1027 of 2048 charcters"
  //
  //  Initially, we can't write to the CRT because PWO is still asserted, and the HP85 bus
  //  is not yet active.
  //
  //  CRT_Log_Buffer    is CRT_LOG_BUFFER_SIZE    long (1 kB when this comment written)
  //  Serial_Log_Buffer is SERIAL_LOG_BUFFER_SIZE long (2 kB when this comment written)
  //
  //  At this point we haven't yet read the CONFIG.TXT file, so we don't yet know if we
  //  should pause booting up until the serial port is active.
  //
