#!/usr/bin/python3

import io, os
import fcntl
import struct
import time

IOCTL_I2C_SLAVE=0x0703


class dione1280(object):

  def __init__(self, bus=6, dev_addr=0x5a):
    self.fr=io.open("/dev/i2c-"+str(bus), "rb", buffering=0)
    self.fw=io.open("/dev/i2c-"+str(bus), "wb", buffering=0)

    fcntl.ioctl(self.fr, IOCTL_I2C_SLAVE, dev_addr)
    fcntl.ioctl(self.fw, IOCTL_I2C_SLAVE, dev_addr)

    self.last_file_op = -1


  def read_reg32(self, reg_addr):
    time.sleep(0.01)
    out=bytearray(reg_addr.to_bytes(4, 'little'))+bytearray([0x04, 0x00])
    self.fw.write(out)
    time.sleep(0.01)
    ret=self.fr.read(6)
    # print(ret)
    val=struct.unpack('<L', ret[2:])
    return val[0]


  def write_reg32(self, reg_addr, val):
    out=bytearray(reg_addr.to_bytes(4, 'little')) \
        +bytearray([0x04, 0x00]) \
        +bytearray(val.to_bytes(4, 'little'))
    time.sleep(0.01)
    ret=self.fw.write(out)
    return ret


  def ack_stop(self):
    self.write_reg32(0x80104,2)
    print(hex(self.read_reg32(0x8010c)))


  def ack_start(self):
    self.write_reg32(0x80104,1)
    print(hex(self.read_reg32(0x8010c)))


  def test_solid_black(self):
    self.ack_stop()
    self.write_reg32(0x8012c,0)


  def test_solid_color(self, color):
    self.ack_stop()
    self.write_reg32(0x8012c,color << 16)


  def read_buf(self, reg_addr, length):
    """! reads <length> bytes from <reg_addr>
    """

    out=bytearray(reg_addr.to_bytes(4, 'little')) \
        + bytearray(length.to_bytes(2, 'little'))
    time.sleep(0.01)
    self.fw.write(out)
    time.sleep(0.01)
    ret=self.fr.read(2+length)
    return ret


  def write_buf(self, reg_addr, buf):
    """! writes <buf> to <reg_addr>
    """

    out=bytearray(reg_addr.to_bytes(4, 'little')) \
        + bytearray(len(buf).to_bytes(2, 'little')) + buf
    time.sleep(0.01)
    self.fw.write(out)
    time.sleep(0.01)
    # print(self.fr.read(2))


  def exec_file_op(self, op):
    """! execute <op> file operation

    sets FileOperationSelector to <op>, sets FileOperationExecute to 1
    and awaits FileOperationStatus to become 0
    """

    if op != self.last_file_op :
      self.last_file_op = op
      self.write_reg32(0x10010008, op)  # FileOperationSelector
    self.write_reg32(0x1001000c, 1)     # FileOperationExecute
    ret = self.read_reg32(0x10010010)   # FileOperationStatus
    while ret == 2 :
      time.sleep(0.01)
      ret = self.read_reg32(0x10010010)
 
    if ret != 0 :
      print( "Failed to execute operation" )
      return False
    else :
      return True


  def open_file(self, idx, write_mode=False):
    """! open file no. <idx>

    @param write_mode  optional - if True, open file in WRITE mode
    """

    self.ack_stop()
    self.write_reg32(0x10010000, idx)  # FileSelector
    sel = self.read_reg32(0x10010000)
    if ( sel != idx ) :
      print( "Failed to select file" )
      return -1
    else :
      print( f'Selected file: {sel}' )

    if not write_mode:
      self.write_reg32(0x10010004, 0)  # FileOpenMode = read
    else:
      self.write_reg32(0x10010004, 1)  # FileOpenMode = write
    if not self.exec_file_op(0) :    # open
      return -1

    ret = self.read_reg32(0x10010018)
    print( f'File size: {ret}' )
    return ret


  def close_file(self):
    if not self.exec_file_op(1) :    # close
      print( "Failed to close file" )
      return False
    else:
      return True


  def read_file(self, offs, length):
    """! read current file into FileAccessBuffer

    sets FileAccessOffset to <offs>, FileAccessLength to <length> and
    executes the file read operation
    """

    retry = 4
    tmo = 0.5
    while retry > 0:
      try:
        self.write_reg32(0x1001001c, offs)    # FileAccessOffset
        self.write_reg32(0x10010020, length)  # FileAccessLength
        if not self.exec_file_op(2) :         # read
          print( "Failed to execute read" )
          return False
        else:
          return True
      except OSError:
        print( f'Retry: {retry}' )
        time.sleep(tmo)
        tmo *= 2
        retry -= 1

    print( "Failed to execute read" )
    return False


  def read_filebuf(self, length):
    """! reads contents of FileAccessBuffer

    According to documentation maximum 1000 bytes can be read
    with a single transaction. read_filebuf() breaks up the
    whole read process into 1000-byte chunks.
    It returns a bytearray.
    Maximum <length> is 4096.
    """

    buf = bytearray([])
    ofs = 0
    while length > 0 :
      read = length
      if read > 1000 :
        read = 1000

      retry = 4
      tmo = 0.5
      while retry > 0:
        try:
          ret = self.read_buf(0x10011000 + ofs, read)
          if ret[0:2] != bytearray([0,0]):
            print(f'read_buf() failed {ret[0:2]}')
            return -1
          buf += ret[2:]
          break
        except OSError:
          print( f'Retry: {retry}' )
          time.sleep(tmo)
          tmo *= 2
          retry -= 1

      if retry == 0:
        raise OSError

      length -= read
      ofs += read
    return buf


  def write_filebuf(self, buf):
    """!

    """

    length = len(buf)
    ofs = 0
    while length > 0:
      wrt = length
      if wrt > 1000:
        wrt = 1000

      retry = 4
      tmo = 0.5
      while retry > 0:
        try:
          self.write_buf(0x10011000 + ofs, buf[ofs:ofs+wrt])
          break
        except OSError:
          print( f'Retry: {retry}' )
          time.sleep(tmo)
          tmo *= 2
          retry -= 1

      if retry == 0:
        raise OSError

      length -= wrt
      ofs += wrt


  def verify_filebuf(self, buf):
    length = len(buf)
    buf2 = self.read_filebuf(length)
    return buf2==buf


  def save_file(self, idx, dst, max_len=-1, ofs=0):
    """! save contents of existing Dione file to filesystem

    @param idx      no. of Dione file to read
    @param dst      name of filesystem file to write to
    @param max_len  optional - no. of bytes to be written
    @param ofs      optional - start offset in Dione file

    it opens the Dione file with self.open_file() and reads it
    in 4096-byte chunks with self.read_filebuf()
    """

    f_dst = io.open(dst, "wb")

    length = self.open_file( idx ) - ofs

    if length > 0 :
      print( f'Length of file #{idx} : {length}' )
    else :
      print( "Failed to open file or invalid <ofs>" )
      return -1

    if max_len >= 0:
      length = max_len
      print( f'Limiting file length to {length} bytes' )

    total = 0
    read = 0
    col = 0

    while length > 0 :
      read = length
      if read > 4096 :
        read = 4096
      if not self.read_file(ofs, read) :
        print( f'Read error at offset {ofs}' )
        return -1

      f_dst.write(self.read_filebuf(read))
      total += read
      ofs += read
      length -= read
      col += 1
      print( ".", end="", flush=True)
      if col >= 64 :
        col = 0
        print()

    print()
    self.close_file()
    f_dst.close()
    print( f'Saved {total} bytes' )
    return total


  def update_file(self, idx, src):
    """! updates Dione file no. <idx>

    """

    length = os.path.getsize(src)
    print( f'Size of {src}: {length}' )

    orig_len = self.open_file(idx, 1)
    print( f'original length: {orig_len}' )
    if orig_len < 0:
      return -1

    f_src = io.open(src, "rb")

    ofs = 0
    total = 0
    col = 0
    while length > 0:
      read = length
      if read > 4096:
        read = 4096
      buf = f_src.read(read)

      retry = 4
      tmo = 0.5
      while retry > 0:
        try:
          self.last_file_op = 3
          self.write_reg32(0x10010008, 3)  # FileOperationSelector
          break
        except OSError:
          print( f'Retry: {retry}' )
          time.sleep(tmo)
          tmo *= 2
          retry -= 1

      if retry == 0:
        print( "Failed to set FileOperationSelector to 'write'" )
        self.close_file()
        f_src.close()
        return -1

      self.write_filebuf(buf)
      if not self.verify_filebuf(buf):
        print( f'Verify error at {ofs}' )
        self.close_file()
        f_src.close()
        return -1

      retry = 4
      tmo = 0.5
      while retry > 0:
        try:
          self.write_reg32(0x1001001c, ofs)     # FileAccessOffset
          self.write_reg32(0x10010020, read)    # FileAccessLength
          if not self.exec_file_op(3) :         # write
            print( "Failed to execute write" )
            self.close_file()
            f_src.close()
            return False
          else:
            break
        except OSError:
          print( f'Retry: {retry}' )
          time.sleep(tmo)
          tmo *= 2
          retry -= 1

      if retry == 0:
        print( "Failed to fill FileAccessBuffer" )
        self.close_file()
        f_src.close()
        return -1

      length -= read
      ofs += read
      total += read
      col += 1
      print( ".", end="", flush=True)
      if col >= 64 :
        col = 0
        print()

    print()
    self.close_file()
    f_src.close()
    print( f'Updated {total} bytes' )
    return total


  def hack_write_file(self, idx, buf_src):
    buf_len = len(buf_src)
    ofs = 0
    start_pad = bytearray([0] * 5)
    while buf_len > 0:
      if self.open_file(idx, 1) < 0:
        print(f'Failed to open file at offset {ofs}')
        return -1

      chunk = buf_len
      if chunk > 1024:
        chunk = 1024
      self.write_filebuf(start_pad + buf_src[ofs:ofs+chunk])

      retry = 4
      tmo = 0.5
      while retry > 0:
        try:
          self.write_reg32(0x1001001c, ofs)     # FileAccessOffset
          self.write_reg32(0x10010020, chunk)   # FileAccessLength
          if not self.exec_file_op(3) :         # write
            print(f'Failed to execute write at offset {ofs}')
            self.close_file()
            return False
          else:
            break
        except OSError:
          print( f'Retry: {retry}' )
          time.sleep(tmo)
          tmo *= 2
          retry -= 1

      if retry == 0:
        print( "Failed to execute write" )
        self.close_file()
        return -1

      if not self.close_file():
        print(f'Failed to close file at offset {ofs}')
        return -1

      ofs += chunk
      buf_len -= chunk

    self.close_file()
    print(f'Written {ofs} bytes')
