package org.apache.hadoop.hdfs.server.namenode;

import java.nio.ByteBuffer;

import org.apache.hadoop.io.nativeio.NativeIO;
import org.apache.hadoop.io.nativeio.NativeIOException;
import org.apache.hadoop.util.NativeCodeLoader;
import org.apache.log4j.LogManager;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

public class nvram_mmap {
	  public static final Logger LOG =
		      LoggerFactory.getLogger(nvram_mmap.class.getName());	
	void nvram_mmap_test (int size) {
		
//		try {
//		long addr = NativeIO.ReturnNVRAMAddress(4096, 4096);
//		int data = 23;
//		NativeIO.putIntTest(addr, data, 0);
//		int temp = NativeIO.readIntTest(addr, 0);
//		LOG.info("temp data = " + data);
//		
//		} catch (NativeIOException e) {
//			e.printStackTrace();
//		}
//		try {
//			if(NativeCodeLoader.isNativeCodeLoaded()){
//			ByteBuffer byteBuffer = ByteBuffer.allocateDirect(100);
//			//LOG.info(byteBuffer);
//			//LOG.info("TEST start");
//			System.out.println(byteBuffer);
//			NativeIO.getMemlockLimit0();
//			//NativeIO.allocateNVRAMBuffer(size);
//			System.out.println(byteBuffer);
//			}
//			//LOG.info(byteBuffer);
//		} catch (NativeIOException e) {
//			// TODO Auto-generated catch block
//			e.printStackTrace();
//		}
	}
	

}
