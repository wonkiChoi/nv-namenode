/**
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
package org.apache.hadoop.hdfs.server.namenode;

import java.io.DataInput;
import java.io.DataOutput;
import java.io.DataOutputStream;
import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.Charset;
import java.util.Arrays;

import org.apache.commons.logging.Log;
import org.apache.commons.logging.LogFactory;
import org.apache.commons.logging.impl.Log4JLogger;
import org.apache.commons.io.Charsets;

import org.apache.hadoop.classification.InterfaceAudience;
import org.apache.hadoop.classification.InterfaceStability;
import org.apache.hadoop.fs.Path;
import org.apache.hadoop.fs.permission.FsPermission;
import org.apache.hadoop.fs.permission.PermissionStatus;
import org.apache.hadoop.hdfs.DFSUtil;
import org.apache.hadoop.hdfs.DeprecatedUTF8;
import org.apache.hadoop.hdfs.protocol.Block;
import org.apache.hadoop.hdfs.protocol.CacheDirectiveInfo;
import org.apache.hadoop.hdfs.protocol.CachePoolInfo;
import org.apache.hadoop.hdfs.protocol.HdfsConstants;
import org.apache.hadoop.hdfs.protocol.LayoutVersion;
import org.apache.hadoop.hdfs.server.blockmanagement.BlockInfoContiguous;
import org.apache.hadoop.hdfs.server.blockmanagement.BlockInfoContiguousUnderConstruction;
import org.apache.hadoop.hdfs.server.common.HdfsServerConstants.BlockUCState;
import org.apache.hadoop.hdfs.server.namenode.snapshot.SnapshotFSImageFormat;
import org.apache.hadoop.hdfs.server.namenode.snapshot.SnapshotFSImageFormat.ReferenceMap;
import org.apache.hadoop.hdfs.util.XMLUtils;
import org.apache.hadoop.hdfs.util.XMLUtils.InvalidXmlException;
import org.apache.hadoop.hdfs.util.XMLUtils.Stanza;
import org.apache.hadoop.io.BooleanWritable;
import org.apache.hadoop.io.IntWritable;
import org.apache.hadoop.io.LongWritable;
import org.apache.hadoop.io.ShortWritable;
import org.apache.hadoop.io.Text;
import org.apache.hadoop.io.WritableUtils;
import org.apache.hadoop.io.nativeio.NativeIO;
import org.apache.hadoop.util.BytesUtil;
import org.xml.sax.ContentHandler;
import org.xml.sax.SAXException;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import com.google.common.base.Preconditions;

/**
 * Static utility functions for serializing various pieces of data in the correct
 * format for the FSImage file.
 *
 * Some members are currently public for the benefit of the Offline Image Viewer
 * which is located outside of this package. These members should be made
 * package-protected when the OIV is refactored.
 */
@InterfaceAudience.Private
@InterfaceStability.Evolving
public class FSImageSerialization {
	static final Log LOG = LogFactory.getLog(FSImageSerialization.class);
  // Static-only class
  private FSImageSerialization() {}
  
  /**
   * In order to reduce allocation, we reuse some static objects. However, the methods
   * in this class should be thread-safe since image-saving is multithreaded, so 
   * we need to keep the static objects in a thread-local.
   */
  static private final ThreadLocal<TLData> TL_DATA =
    new ThreadLocal<TLData>() {
    @Override
    protected TLData initialValue() {
      return new TLData();
    }
  };

  /**
   * Simple container "struct" for threadlocal data.
   */
  static private final class TLData {
    final DeprecatedUTF8 U_STR = new DeprecatedUTF8();
    final ShortWritable U_SHORT = new ShortWritable();
    final IntWritable U_INT = new IntWritable();
    final LongWritable U_LONG = new LongWritable();
    final FsPermission FILE_PERM = new FsPermission((short) 0);
    final BooleanWritable U_BOOLEAN = new BooleanWritable();
  }

  private static void writePermissionStatus(INodeAttributes inode,
      DataOutput out) throws IOException {
    final FsPermission p = TL_DATA.get().FILE_PERM;
    p.fromShort(inode.getFsPermissionShort());
    PermissionStatus.write(out, inode.getUserName(), inode.getGroupName(), p);
  }
  
	private static int writePermissionStatus(INodeAttributes inode, int new_offset, int new_pos) throws IOException {
		final FsPermission p = TL_DATA.get().FILE_PERM;
		p.fromShort(inode.getFsPermissionShort());
		int new_posi = 0;
		byte[] byte_usr = inode.getUserName().getBytes();
		byte[] byte_group = inode.getGroupName().getBytes();

		new_posi = NativeIO.putIntToNVRAM(4096, new_offset, byte_usr.length, new_pos);
		new_posi = NativeIO.putBAToNVRAM(4096, new_offset, byte_usr, new_posi);
		new_posi = NativeIO.putIntToNVRAM(4096, new_offset, byte_group.length, new_posi);
		new_posi = NativeIO.putBAToNVRAM(4096, new_offset, byte_group, new_posi);

		int s = (p.getStickyBit() ? 1 << 9 : 0) | (p.getUserAction().ordinal() << 6)
				| (p.getGroupAction().ordinal() << 3) | p.getOtherAction().ordinal();

		new_posi = NativeIO.putIntToNVRAM(4096, new_offset, s, new_posi);
		return new_posi;
	}
  
  private static void writePermissionStatus(INodeAttributes inode,
	     ByteBuffer out) throws IOException {
	    final FsPermission p = TL_DATA.get().FILE_PERM;
	    p.fromShort(inode.getFsPermissionShort());
	    PermissionStatus.write(out, inode.getUserName(), inode.getGroupName(), p);
	  }

	private static int writeBlocks(final Block[] blocks, int new_offset, int new_pos) throws IOException {
		if (blocks == null | blocks.length == 0) {
			int new_new_pos = 0;
			new_new_pos = NativeIO.putIntToNVRAM(4096, new_offset, 0, new_pos);
			return new_new_pos;
		} else {
			int new_new_pos = 0;
			new_new_pos = NativeIO.putIntToNVRAM(4096, new_offset, 1, new_pos);
			for (Block blk : blocks) {
//				 LOG.info("writeBlocks blk information = " + blk.getBlockId()+
//				 " " + blk.getNumBytes() + " " + blk.getGenerationStamp());
				new_new_pos = NativeIO.putLongToNVRAM(4096, new_offset, blk.getBlockId(), new_new_pos);
				new_new_pos = NativeIO.putLongToNVRAM(4096, new_offset, blk.getNumBytes(), new_new_pos);
				new_new_pos = NativeIO.putLongToNVRAM(4096, new_offset, blk.getGenerationStamp(), new_new_pos);
			}
			return new_new_pos;
		}
	}
  
  private static void writeBlocks(final Block[] blocks,
	      final DataOutput out) throws IOException {
	    if (blocks == null) {
	      out.writeInt(0);
	    } else {
	      out.writeInt(blocks.length);
	      for (Block blk : blocks) {
	        blk.write(out);
	      }
	    }
	  }
  
  private static void writeBlocks(final Block[] blocks,
	      final ByteBuffer out) throws IOException {
	    if (blocks == null | blocks.length == 0 ) {
	      out.putInt(0);
	    } else {
	    	int current = out.position();
	      out.putInt(blocks.length);
	      out.position(current);
	      int length = out.getInt();
	      LOG.info("block length = " + blocks.length + " in length " + length);
	      for (Block blk : blocks) {
	        blk.write(out);
	      }
	    }
	  }

  // Helper function that reads in an INodeUnderConstruction
  // from the input stream
  //
  static INodeFile readINodeUnderConstruction(
      DataInput in, FSNamesystem fsNamesys, int imgVersion)
      throws IOException {
    byte[] name = readBytes(in);
    long inodeId = NameNodeLayoutVersion.supports(
        LayoutVersion.Feature.ADD_INODE_ID, imgVersion) ? in.readLong()
        : fsNamesys.dir.allocateNewInodeId();
    short blockReplication = in.readShort();
    long modificationTime = in.readLong();
    long preferredBlockSize = in.readLong();
  
    int numBlocks = in.readInt();
    BlockInfoContiguous[] blocks = new BlockInfoContiguous[numBlocks];
    Block blk = new Block();
    int i = 0;
    for (; i < numBlocks-1; i++) {
      blk.readFields(in);
      blocks[i] = new BlockInfoContiguous(blk, blockReplication);
    }
    // last block is UNDER_CONSTRUCTION
    if(numBlocks > 0) {
      blk.readFields(in);
      blocks[i] = new BlockInfoContiguousUnderConstruction(
        blk, blockReplication, BlockUCState.UNDER_CONSTRUCTION, null);
    }
    PermissionStatus perm = PermissionStatus.read(in);
    String clientName = readString(in);
    String clientMachine = readString(in);

    // We previously stored locations for the last block, now we
    // just record that there are none
    int numLocs = in.readInt();
    assert numLocs == 0 : "Unexpected block locations";

    // Images in the pre-protobuf format will not have the lazyPersist flag,
    // so it is safe to pass false always.
    INodeFile file = new INodeFile(inodeId, name, perm, modificationTime,
        modificationTime, blocks, blockReplication, preferredBlockSize, (byte)0);
    file.toUnderConstruction(clientName, clientMachine);
    return file;
  }
  
  static INodeFile readINodeUnderConstruction(
	      ByteBuffer in, FSNamesystem fsNamesys, int imgVersion)
	      throws IOException {
	    byte[] name = readBytes(in);
	    long inodeId = NameNodeLayoutVersion.supports(
	        LayoutVersion.Feature.ADD_INODE_ID, imgVersion) ? in.getLong()
	        : fsNamesys.dir.allocateNewInodeId();
	    short blockReplication = in.getShort();
	    long modificationTime = in.getLong();
	    long preferredBlockSize = in.getLong();
	  
	    int numBlocks = in.getInt();
	    BlockInfoContiguous[] blocks = new BlockInfoContiguous[numBlocks];
	    Block blk = new Block();
	    int i = 0;
	    for (; i < numBlocks-1; i++) {
	      blk.readFields(in);
	      blocks[i] = new BlockInfoContiguous(blk, blockReplication);
	    }
	    // last block is UNDER_CONSTRUCTION
	    if(numBlocks > 0) {
	      blk.readFields(in);
	      blocks[i] = new BlockInfoContiguousUnderConstruction(
	        blk, blockReplication, BlockUCState.UNDER_CONSTRUCTION, null);
	    }
	    PermissionStatus perm = PermissionStatus.read(in);
	    String clientName = readString(in);
	    String clientMachine = readString(in);

	    // We previously stored locations for the last block, now we
	    // just record that there are none
	    int numLocs = in.getInt();
	    assert numLocs == 0 : "Unexpected block locations";

	    // Images in the pre-protobuf format will not have the lazyPersist flag,
	    // so it is safe to pass false always.
	    INodeFile file = new INodeFile(inodeId, name, perm, modificationTime,
	        modificationTime, blocks, blockReplication, preferredBlockSize, (byte)0);
	    file.toUnderConstruction(clientName, clientMachine);
	    return file;
	  }
  
  static INodeFile readINodeFile(
	      ByteBuffer in)
	      throws IOException {
	    LOG.info("bybuf = " + in);
	    byte[] name = readBytesMod(in);
	    LOG.info(in);
	    long inodeId = in.getLong();
	    LOG.info(in +" READ : inodid =" + inodeId);
	    short blockReplication = in.getShort();
	    LOG.info(in + " READ : replication =" + blockReplication);
	    long modificationTime = in.getLong();
	    LOG.info(in + " READ : modification =" + modificationTime);
	    long accessTime = in.getLong();
	    LOG.info(in +" READ : access =" + accessTime);
	    long preferredBlockSize = in.getLong();
	    LOG.info(in + "READ : preferred =" + preferredBlockSize);
	  
	    int writeUnderConstruction = in.getInt();
	    String clientName = null;
	    String clientMachine = null;
	    if(writeUnderConstruction == 1) {
		    clientName = readStringMod(in);
		    clientMachine = readStringMod(in);	    	
	    } 
	    int numBlocks = in.getInt();
	    LOG.info("READ : writeunder =" + writeUnderConstruction);
	    LOG.info("READ : numblock =" + numBlocks);
	    INodeFile file = null;
	    if(numBlocks == 0) {
		    PermissionStatus perm = PermissionStatus.read(in);
		    
		    file = new INodeFile(inodeId, name, perm, modificationTime,
		            accessTime, BlockInfoContiguous.EMPTY_ARRAY, blockReplication, preferredBlockSize, (byte)0);
	    } else {
	    BlockInfoContiguous[] blocks = new BlockInfoContiguous[numBlocks];
	    Block blk = new Block();
//	    if( writeUnderConstruction == 1) {
//	    int i = 0;
//	    for (; i < numBlocks-1; i++) {
//	      blk.readFields(in);
//	      blocks[i] = new BlockInfoContiguous(blk, blockReplication);
//	    }
//	    // last block is UNDER_CONSTRUCTION
//	    if(numBlocks > 0) {
//	      blk.readFields(in);
//	      blocks[i] = new BlockInfoContiguousUnderConstruction(
//	        blk, blockReplication, BlockUCState.UNDER_CONSTRUCTION, null);
//	    }
//	    } else if (writeUnderConstruction == 2) {
	      int i = 0;
		    for (; i < numBlocks; i++) {
		      blk.readFields(in);
		      blocks[i] = new BlockInfoContiguous(blk, blockReplication);
		    }
	  //  }  

	    PermissionStatus perm = PermissionStatus.read(in);  
	    file = new INodeFile(inodeId, name, perm, modificationTime,
	        accessTime, blocks, blockReplication, preferredBlockSize, (byte)0);
	    if(writeUnderConstruction == 1) {
	    file.toUnderConstruction(clientName, clientMachine);
	    }
	    }
	    return file;
	  }
  
	static INodeFile readINodeFile(int new_offset, int pos) throws IOException {
		int length = NativeIO.readIntFromNVRAM(4096, new_offset, pos);
		int new_pos = pos + 4;
		byte[] name = NativeIO.readBAFromNVRAM(4096, new_offset, new_pos, length);
		new_pos = new_pos + 100;

		long inodeId = NativeIO.readLongFromNVRAM(4096, new_offset, new_pos);
		new_pos = new_pos + 8;
		short blockReplication = (short) NativeIO.readLongFromNVRAM(4096, new_offset, new_pos);
		new_pos = new_pos + 8;
		long modificationTime = NativeIO.readLongFromNVRAM(4096, new_offset, new_pos);
		new_pos = new_pos + 8;
		long accessTime = NativeIO.readLongFromNVRAM(4096, new_offset, new_pos);
		new_pos = new_pos + 8;
		long preferredBlockSize = NativeIO.readLongFromNVRAM(4096, new_offset, new_pos);
		new_pos = new_pos + 8;
		int writeUnderConstruction = NativeIO.readIntFromNVRAM(4096, new_offset, new_pos);

		new_pos = new_pos + 4;
		String clientName = null;
		String clientMachine = null;

		int size = NativeIO.readIntFromNVRAM(4096, new_offset, new_pos);
		new_pos = new_pos + 4;
		byte[] str = NativeIO.readBAFromNVRAM(4096, new_offset, new_pos, size);
		new_pos = new_pos + 100;
		clientName = new String(str);

		int size_second = NativeIO.readIntFromNVRAM(4096, new_offset, new_pos);
		new_pos = new_pos + 4;
		byte[] str_second = NativeIO.readBAFromNVRAM(4096, new_offset, new_pos, size_second);
		new_pos = new_pos + 100;
		clientMachine = new String(str_second);

		PermissionStatus perm = PermissionStatus.read(new_offset, new_pos);

		INodeFile file = null;
		//file.pos = perm.pos;
		new_pos = perm.pos;
		int numBlocks = NativeIO.readIntFromNVRAM(4096, new_offset, new_pos);
		new_pos = new_pos + 4;

		if (numBlocks == 0) {
			file = new INodeFile(inodeId, name, perm, modificationTime, accessTime, BlockInfoContiguous.EMPTY_ARRAY,
					blockReplication, preferredBlockSize, HdfsConstants.HOT_STORAGE_POLICY_ID);
			if (writeUnderConstruction == 1) {
				file.toUnderConstruction(clientName, clientMachine);
			}
		} else {
			BlockInfoContiguous[] blocks = new BlockInfoContiguous[numBlocks];
			Block blk = new Block();
			int i = 0;
			for (; i < numBlocks; i++) {
				blk.setBlockId(NativeIO.readLongFromNVRAM(4096, new_offset, new_pos));
				new_pos = new_pos + 8;
				blk.setNumBytes(NativeIO.readLongFromNVRAM(4096, new_offset, new_pos));
				new_pos = new_pos + 8;
				blk.setGenerationStamp(NativeIO.readLongFromNVRAM(4096, new_offset, new_pos));
				new_pos = new_pos + 8;
				blocks[i] = new BlockInfoContiguous(blk, blockReplication);

//				LOG.info("block read info = " + blocks[i].getBlockId() + " " + blocks[i].getNumBytes() + " "
//						+ blocks[i].getGenerationStamp());
			}
			file = new INodeFile(inodeId, name, perm, modificationTime, accessTime, blocks, blockReplication, preferredBlockSize,
					HdfsConstants.HOT_STORAGE_POLICY_ID);
			if (writeUnderConstruction == 1) {
				file.toUnderConstruction(clientName, clientMachine);
			}
		}
		file.pos = new_pos;
		return file;
	}
  
	static INodeDirectory readINodeDir(int new_offset, int pos) throws IOException {

		int length = NativeIO.readIntFromNVRAM(4096, new_offset, pos);
		int new_pos = pos + 4;
		byte[] name = NativeIO.readBAFromNVRAM(4096, new_offset, new_pos, length);
		new_pos = new_pos + 100;

		long inodeId = NativeIO.readLongFromNVRAM(4096, new_offset, new_pos);
		new_pos = new_pos + 8;

		PermissionStatus perm = PermissionStatus.read(new_offset, new_pos);

		INodeDirectory dir = new INodeDirectory(inodeId, name, perm, 0L);
		dir.pos = perm.pos;

		return dir;
	}

  // Helper function that writes an INodeUnderConstruction
  // into the output stream
  //
  static void writeINodeUnderConstruction(DataOutputStream out, INodeFile cons,
      String path) throws IOException {
    writeString(path, out);
    out.writeLong(cons.getId());
    out.writeShort(cons.getFileReplication());
    out.writeLong(cons.getModificationTime());
    out.writeLong(cons.getPreferredBlockSize());

    writeBlocks(cons.getBlocks(), out);
    cons.getPermissionStatus().write(out);

    FileUnderConstructionFeature uc = cons.getFileUnderConstructionFeature();
    writeString(uc.getClientName(), out);
    writeString(uc.getClientMachine(), out);

    out.writeInt(0); //  do not store locations of last block
  }
  
 
  /**
   * Serialize a {@link INodeFile} node
   * @param node The node to write
   * @param out The {@link DataOutputStream} where the fields are written
   * @param writeBlock Whether to write block information
   */
  public static void writeINodeFile(INodeFile file, DataOutput out,
      boolean writeUnderConstruction) throws IOException {
    writeLocalName(file, out);
    out.writeLong(file.getId());
    out.writeShort(file.getFileReplication());
    out.writeLong(file.getModificationTime());
    out.writeLong(file.getAccessTime());
    out.writeLong(file.getPreferredBlockSize());

    writeBlocks(file.getBlocks(), out);
    SnapshotFSImageFormat.saveFileDiffList(file, out);

    if (writeUnderConstruction) {
      if (file.isUnderConstruction()) {
        out.writeBoolean(true);
        final FileUnderConstructionFeature uc = file.getFileUnderConstructionFeature();
        writeString(uc.getClientName(), out);
        writeString(uc.getClientMachine(), out);
      } else {
        out.writeBoolean(false);
      }
    }

    writePermissionStatus(file, out);
  }
  // deprecated
  public static void writeINodeFile(INodeFile file, ByteBuffer out,
	      boolean writeUnderConstruction) throws IOException {
	    LOG.info("out = " + out);
	    writeLocalName(file, out);
	    LOG.info("out2 = " + out);
	    long fileid = file.getId();
	    int current = out.position();

	  }
  
	public static int writeINodeFile(INodeFile file, int new_offset, boolean writeUnderConstruction, int position, int block)
			throws IOException {
		int new_pos = 0;
		if ( block == 0 ){
		final byte[] name = file.getLocalNameBytes();
		new_pos = NativeIO.putIntToNVRAM(4096, new_offset, name.length, position);
		new_pos = NativeIO.putBAToNVRAM(4096, new_offset, name, new_pos);

		long fileid = file.getId();
		long testid;
		new_pos = NativeIO.putLongToNVRAM(4096, new_offset, fileid, new_pos);
		long replication = (long) file.getFileReplication();
		new_pos = NativeIO.putLongToNVRAM(4096, new_offset, replication, new_pos);
		long modificationTime = file.getModificationTime();
		new_pos = NativeIO.putLongToNVRAM(4096, new_offset, modificationTime, new_pos);
		long accessTime = file.getAccessTime();
		new_pos = NativeIO.putLongToNVRAM(4096, new_offset, accessTime, new_pos);
		long preferredBlock = file.getPreferredBlockSize();
		new_pos = NativeIO.putLongToNVRAM(4096, new_offset, preferredBlock, new_pos);

		if (writeUnderConstruction) {
			new_pos = NativeIO.putIntToNVRAM(4096, new_offset, 1, new_pos);
		} else {
			new_pos = NativeIO.putIntToNVRAM(4096, new_offset, 2, new_pos);
		}
		final FileUnderConstructionFeature uc = file.getFileUnderConstructionFeature();

		byte[] clientName = null;
		byte[] clientMachine = null;
		if (uc != null) {
			clientName = uc.getClientName().getBytes();
			clientMachine = uc.getClientMachine().getBytes();
		} else {
			clientName = "temporary".getBytes();
			clientMachine = "temporary".getBytes();
		}
		new_pos = NativeIO.putIntToNVRAM(4096, new_offset, clientName.length, new_pos);
		new_pos = NativeIO.putBAToNVRAM(4096, new_offset, clientName, new_pos);

		new_pos = NativeIO.putIntToNVRAM(4096, new_offset, clientMachine.length, new_pos);

		new_pos = NativeIO.putBAToNVRAM(4096, new_offset, clientMachine, new_pos);
		new_pos = writePermissionStatus(file, new_offset, new_pos);
//		LOG.info("new_pos after write Permission = " + new_pos);
		new_pos = writeBlocks(file.getBlocks(), new_offset, new_pos);
		} else {
		new_pos = writeBlocks(file.getBlocks(), new_offset, 576);
		}
		return new_pos;
	}

  /** Serialize an {@link INodeFileAttributes}. */
  public static void writeINodeFileAttributes(INodeFileAttributes file,
      DataOutput out) throws IOException {
    writeLocalName(file, out);
    writePermissionStatus(file, out);
    out.writeLong(file.getModificationTime());
    out.writeLong(file.getAccessTime());

    out.writeShort(file.getFileReplication());
    out.writeLong(file.getPreferredBlockSize());
  }

  public static void writeINodeFileAttributes(INodeFileAttributes file,
	      ByteBuffer out) throws IOException {
	    writeLocalName(file, out);
	    writePermissionStatus(file, out);
	    out.putLong(file.getModificationTime());
	    out.putLong(file.getAccessTime());

	    out.putShort(file.getFileReplication());
	    out.putLong(file.getPreferredBlockSize());
	  }
  private static void writeQuota(QuotaCounts quota, DataOutput out)
      throws IOException {
    out.writeLong(quota.getNameSpace());
    out.writeLong(quota.getStorageSpace());
  }
  
  private static void writeQuota(QuotaCounts quota, ByteBuffer out)
	      throws IOException {
	    out.putLong(quota.getNameSpace());
	    out.putLong(quota.getStorageSpace());
	  }

  /**
   * Serialize a {@link INodeDirectory}
   * @param node The node to write
   * @param out The {@link DataOutput} where the fields are written
   */
  public static void writeINodeDirectory(INodeDirectory node, DataOutput out)
	      throws IOException {
	    writeLocalName(node, out);
	    out.writeLong(node.getId());
	    out.writeShort(0);  // replication
	    out.writeLong(node.getModificationTime());
	    out.writeLong(0);   // access time
	    out.writeLong(0);   // preferred block size
	    out.writeInt(-1);   // # of blocks

	    writeQuota(node.getQuotaCounts(), out);

	    if (node.isSnapshottable()) {
	      out.writeBoolean(true);
	    } else {
	      out.writeBoolean(false);
	      out.writeBoolean(node.isWithSnapshot());
	    }

	    writePermissionStatus(node, out);
	  }
  
  public static int writeINodeDirectory(INodeDirectory node, int new_offset, int position)
      throws IOException {
	  
	  int new_pos = 0;
	  final byte[] name = node.getLocalNameBytes();
	  new_pos = NativeIO.putIntToNVRAM(4096, new_offset, name.length, position);
	  new_pos = NativeIO.putBAToNVRAM(4096, new_offset, name, new_pos);
	  
	  new_pos = NativeIO.putLongToNVRAM(4096, new_offset, node.getId(), new_pos);
	
    new_pos = writePermissionStatus(node, new_offset, new_pos);
    return new_pos;
  }
  
  public static void writeINodeDirectory(INodeDirectory node, ByteBuffer out)
	      throws IOException {
	    writeLocalName(node, out);
	    out.putLong(node.getId());
	    out.putShort((short) 0);  // replication
	    out.putLong(node.getModificationTime());
	    out.putLong(0);   // access time
	    out.putLong(0);   // preferred block size
	    out.putInt(-1);   // # of blocks

	    writeQuota(node.getQuotaCounts(), out);

	    if (node.isSnapshottable()) {
	      out.put((byte)1);
	    } else {
	      out.put((byte)0);
	      if(node.isWithSnapshot() == true) {
	      out.put((byte) 1);
	      } else {
	    	  out.put((byte) 0);
	      }
	    }

	    writePermissionStatus(node, out);
	  }
  
  public static void writeINodeDirectory(INodeDirectory node, ByteBuffer out, int position)
	      throws IOException {
	    writeLocalName(node, out);
	    out.putLong(node.getId());
	    out.putShort((short) 0);  // replication
	    out.putLong(node.getModificationTime());
	    out.putLong(0);   // access time
	    out.putLong(0);   // preferred block size
	    out.putInt(-1);   // # of blocks

	    writeQuota(node.getQuotaCounts(), out);

	    if (node.isSnapshottable()) {
	      out.put((byte)1);
	    } else {
	      out.put((byte)0);
	      if(node.isWithSnapshot() == true) {
	      out.put((byte) 1);
	      } else {
	    	  out.put((byte) 0);
	      }
	    }

	    writePermissionStatus(node, out);
	  }

  /**
   * Serialize a {@link INodeDirectory}
   * @param a The node to write
   * @param out The {@link DataOutput} where the fields are written
   */
  public static void writeINodeDirectoryAttributes(
      INodeDirectoryAttributes a, DataOutput out) throws IOException {
    writeLocalName(a, out);
    writePermissionStatus(a, out);
    out.writeLong(a.getModificationTime());
    writeQuota(a.getQuotaCounts(), out);
  }
  
  public static void writeINodeDirectoryAttributes(
	      INodeDirectoryAttributes a, ByteBuffer out) throws IOException {
	    writeLocalName(a, out);
	    writePermissionStatus(a, out);
	    out.putLong(a.getModificationTime());
	    writeQuota(a.getQuotaCounts(), out);
	  }

  /**
   * Serialize a {@link INodeSymlink} node
   * @param node The node to write
   * @param out The {@link DataOutput} where the fields are written
   */
  private static void writeINodeSymlink(INodeSymlink node, DataOutput out)
      throws IOException {
    writeLocalName(node, out);
    out.writeLong(node.getId());
    out.writeShort(0);  // replication
    out.writeLong(0);   // modification time
    out.writeLong(0);   // access time
    out.writeLong(0);   // preferred block size
    out.writeInt(-2);   // # of blocks

    Text.writeString(out, node.getSymlinkString());
    writePermissionStatus(node, out);
  }
  
  private static void writeINodeSymlink(INodeSymlink node, ByteBuffer out)
	      throws IOException {
	    writeLocalName(node, out);
	    out.putLong(node.getId());
	    out.putShort((short)0);  // replication
	    out.putLong(0);   // modification time
	    out.putLong(0);   // access time
	    out.putLong(0);   // preferred block size
	    out.putInt(-2);   // # of blocks

	    Text.writeString(out, node.getSymlinkString());
	    writePermissionStatus(node, out);
	  }

  /** Serialize a {@link INodeReference} node */
  private static void writeINodeReference(INodeReference ref, DataOutput out,
      boolean writeUnderConstruction, ReferenceMap referenceMap
      ) throws IOException {
    writeLocalName(ref, out);
    out.writeLong(ref.getId());
    out.writeShort(0);  // replication
    out.writeLong(0);   // modification time
    out.writeLong(0);   // access time
    out.writeLong(0);   // preferred block size
    out.writeInt(-3);   // # of blocks

    final boolean isWithName = ref instanceof INodeReference.WithName;
    out.writeBoolean(isWithName);

    if (!isWithName) {
      Preconditions.checkState(ref instanceof INodeReference.DstReference);
      // dst snapshot id
      out.writeInt(((INodeReference.DstReference) ref).getDstSnapshotId());
    } else {
      out.writeInt(((INodeReference.WithName) ref).getLastSnapshotId());
    }

    final INodeReference.WithCount withCount
        = (INodeReference.WithCount)ref.getReferredINode();
    referenceMap.writeINodeReferenceWithCount(withCount, out,
        writeUnderConstruction);
  }
  
  private static void writeINodeReference(INodeReference ref, ByteBuffer out,
	      boolean writeUnderConstruction, ReferenceMap referenceMap
	      ) throws IOException {
	    writeLocalName(ref, out);
	    out.putLong(ref.getId());
	    out.putShort((short)0);  // replication
	    out.putLong(0);   // modification time
	    out.putLong(0);   // access time
	    out.putLong(0);   // preferred block size
	    out.putInt(-3);   // # of blocks

	    final boolean isWithName = ref instanceof INodeReference.WithName;
	    if(isWithName == true) {
	    out.put((byte)1);
	    } else {
	    	out.put((byte)0);
	    }
	    if (!isWithName) {
	      Preconditions.checkState(ref instanceof INodeReference.DstReference);
	      // dst snapshot id
	      out.putInt(((INodeReference.DstReference) ref).getDstSnapshotId());
	    } else {
	      out.putInt(((INodeReference.WithName) ref).getLastSnapshotId());
	    }

	    final INodeReference.WithCount withCount
	        = (INodeReference.WithCount)ref.getReferredINode();
	    referenceMap.writeINodeReferenceWithCount(withCount, out,
	        writeUnderConstruction);
	  }

  /**
   * Save one inode's attributes to the image.
   */
  public static void saveINode2Image(INode node, DataOutput out,
      boolean writeUnderConstruction, ReferenceMap referenceMap)
      throws IOException {
    if (node.isReference()) {
      writeINodeReference(node.asReference(), out, writeUnderConstruction,
          referenceMap);
    } else if (node.isDirectory()) {
      writeINodeDirectory(node.asDirectory(), out);
    } else if (node.isSymlink()) {
      writeINodeSymlink(node.asSymlink(), out);
    } else if (node.isFile()) {
      writeINodeFile(node.asFile(), out, writeUnderConstruction);
    }
  }
  
  public static void saveINode2Image(INode node, ByteBuffer out,
	      boolean writeUnderConstruction, ReferenceMap referenceMap)
	      throws IOException {
	    if (node.isReference()) {
	      writeINodeReference(node.asReference(), out, writeUnderConstruction,
	          referenceMap);
	    } else if (node.isDirectory()) {
	      writeINodeDirectory(node.asDirectory(), out);
	    } else if (node.isSymlink()) {
	      writeINodeSymlink(node.asSymlink(), out);
	    } else if (node.isFile()) {
	      writeINodeFile(node.asFile(), out, writeUnderConstruction);
	    }
	  }

  // This should be reverted to package private once the ImageLoader
  // code is moved into this package. This method should not be called
  // by other code.
  @SuppressWarnings("deprecation")
  public static String readString(DataInput in) throws IOException {
    DeprecatedUTF8 ustr = TL_DATA.get().U_STR;
    ustr.readFields(in);
    return ustr.toStringChecked();
  }
  
  @SuppressWarnings("deprecation")
public static String readString(ByteBuffer in) throws IOException {
	    DeprecatedUTF8 ustr = TL_DATA.get().U_STR;
	    ustr.readFields(in);
	    return ustr.toStringChecked();
	  }
  
  public static String readStringMod(ByteBuffer in) throws IOException {
	  int size = in.getInt();
	  LOG.info("read size = " + size);
	  byte[] str = new byte[size];
	  in.get(str);
	  //String ret = null;
//	  try {
//	  //ret = (String) BytesUtil.toObject(str);
		String ret = new String(str);
	  LOG.info("string = " + ret);
//	  } catch ( ClassNotFoundException e) {
//		  LOG.info("class not found");
//	  }
	  return ret;
	  }

  static String readString_EmptyAsNull(DataInput in) throws IOException {
    final String s = readString(in);
    return s.isEmpty()? null: s;
  }

  @SuppressWarnings("deprecation")
  public static void writeString(String str, DataOutput out) throws IOException {
    DeprecatedUTF8 ustr = TL_DATA.get().U_STR;
    ustr.set(str);
    ustr.write(out);
  }
  
  @SuppressWarnings("deprecation")
  public static void writeString(String str, ByteBuffer out) throws IOException {
    DeprecatedUTF8 ustr = TL_DATA.get().U_STR;
    ustr.set(str);
    ustr.write(out);
  }
  
  public static void writeStringMod(String str, ByteBuffer out) throws IOException {
	  
//	  try {
		LOG.info("str = " + str);
		// str.getBytes();
	  //byte[] bytestr = BytesUtil.toByteArray(str);
	  byte[] bytestr = str.getBytes();
		//bytestr.toString();
	  LOG.info("out = " + out + " length " + bytestr.length);
	  int current = out.position();
	  out.putInt((int)bytestr.length);
	  out.position(current);
	  int size = out.getInt();
	  LOG.info("out = " + out + " length " + bytestr.length + " size = " + size);
	  out.put(bytestr);
	  LOG.info("out = " + out );
//	  } catch (IOException e) {
//		  LOG.info("IOEXCEPTION");
//	  }
	  }
  
  /** read the long value */
  static long readLong(DataInput in) throws IOException {
    LongWritable uLong = TL_DATA.get().U_LONG;
    uLong.readFields(in);
    return uLong.get();
  }

  /** write the long value */
  static void writeLong(long value, DataOutputStream out) throws IOException {
    LongWritable uLong = TL_DATA.get().U_LONG;
    uLong.set(value);
    uLong.write(out);
  }
  
  /** read the boolean value */
  static boolean readBoolean(DataInput in) throws IOException {
    BooleanWritable uBoolean = TL_DATA.get().U_BOOLEAN;
    uBoolean.readFields(in);
    return uBoolean.get();
  }
  
  /** write the boolean value */
  static void writeBoolean(boolean value, DataOutputStream out) 
      throws IOException {
    BooleanWritable uBoolean = TL_DATA.get().U_BOOLEAN;
    uBoolean.set(value);
    uBoolean.write(out);
  }
  
  /** write the byte value */
  static void writeByte(byte value, DataOutputStream out)
      throws IOException {
    out.write(value);
  }

  /** read the int value */
  static int readInt(DataInput in) throws IOException {
    IntWritable uInt = TL_DATA.get().U_INT;
    uInt.readFields(in);
    return uInt.get();
  }
  
  /** write the int value */
  static void writeInt(int value, DataOutputStream out) throws IOException {
    IntWritable uInt = TL_DATA.get().U_INT;
    uInt.set(value);
    uInt.write(out);
  }

  /** read short value */
  static short readShort(DataInput in) throws IOException {
    ShortWritable uShort = TL_DATA.get().U_SHORT;
    uShort.readFields(in);
    return uShort.get();
  }

  /** write short value */
  static void writeShort(short value, DataOutputStream out) throws IOException {
    ShortWritable uShort = TL_DATA.get().U_SHORT;
    uShort.set(value);
    uShort.write(out);
  }
  
  // Same comments apply for this method as for readString()
  @SuppressWarnings("deprecation")
  public static byte[] readBytes(DataInput in) throws IOException {
    DeprecatedUTF8 ustr = TL_DATA.get().U_STR;
    ustr.readFields(in);
    int len = ustr.getLength();
    byte[] bytes = new byte[len];
    System.arraycopy(ustr.getBytes(), 0, bytes, 0, len);
    return bytes;
  }
  
  @SuppressWarnings("deprecation")
public static byte[] readBytes(ByteBuffer in) throws IOException {
	    LOG.info("read byte???");
	    DeprecatedUTF8 ustr = TL_DATA.get().U_STR;
	    ustr.readFields(in);
	    int len = ustr.getLength();
	    byte[] bytes = new byte[len];
	    System.arraycopy(ustr.getBytes(), 0, bytes, 0, len);
	    return bytes;
	  }
  
  public static byte[] readBytesMod(ByteBuffer in) throws IOException {
	    LOG.info("read mod byte???");
	    short length = in.getShort();
	    byte[] bytes = new byte[length];
	    in.get(bytes);
	    return bytes;
	  }

  public static byte readByte(DataInput in) throws IOException {
    return in.readByte();
  }

  /**
   * Reading the path from the image and converting it to byte[][] directly
   * this saves us an array copy and conversions to and from String
   * @param in input to read from
   * @return the array each element of which is a byte[] representation 
   *            of a path component
   * @throws IOException
   */
  @SuppressWarnings("deprecation")
  public static byte[][] readPathComponents(DataInput in)
      throws IOException {
    DeprecatedUTF8 ustr = TL_DATA.get().U_STR;
    
    ustr.readFields(in);
    return DFSUtil.bytes2byteArray(ustr.getBytes(),
      ustr.getLength(), (byte) Path.SEPARATOR_CHAR);
  }
  
  public static byte[] readLocalName(DataInput in) throws IOException {
    byte[] createdNodeName = new byte[in.readShort()];
    in.readFully(createdNodeName);
    return createdNodeName;
  }

  private static void writeLocalName(INodeAttributes inode, DataOutput out)
      throws IOException {
    final byte[] name = inode.getLocalNameBytes();
    writeBytes(name, out);
  }
  private static void writeLocalName(INodeAttributes inode, ByteBuffer out)
	      throws IOException {
	   final byte[] name = inode.getLocalNameBytes();
	   writeBytes(name, out);
	  }
   
  public static void writeBytes(byte[] data, DataOutput out)
      throws IOException {
    out.writeShort(data.length);
    out.write(data);
  }
  public static void writeBytes(byte[] data, ByteBuffer out)
	      throws IOException {
	  out.putShort((short)data.length);
	  out.put(data);
	  }

  /**
   * Write an array of blocks as compactly as possible. This uses
   * delta-encoding for the generation stamp and size, following
   * the principle that genstamp increases relatively slowly,
   * and size is equal for all but the last block of a file.
   */
  public static void writeCompactBlockArray(
      Block[] blocks, DataOutputStream out) throws IOException {
    WritableUtils.writeVInt(out, blocks.length);
    Block prev = null;
    for (Block b : blocks) {
      long szDelta = b.getNumBytes() -
          (prev != null ? prev.getNumBytes() : 0);
      long gsDelta = b.getGenerationStamp() -
          (prev != null ? prev.getGenerationStamp() : 0);
      out.writeLong(b.getBlockId()); // blockid is random
      WritableUtils.writeVLong(out, szDelta);
      WritableUtils.writeVLong(out, gsDelta);
      prev = b;
    }
  }
  
  public static Block[] readCompactBlockArray(
      DataInput in, int logVersion) throws IOException {
    int num = WritableUtils.readVInt(in);
    if (num < 0) {
      throw new IOException("Invalid block array length: " + num);
    }
    Block prev = null;
    Block[] ret = new Block[num];
    for (int i = 0; i < num; i++) {
      long id = in.readLong();
      long sz = WritableUtils.readVLong(in) +
          ((prev != null) ? prev.getNumBytes() : 0);
      long gs = WritableUtils.readVLong(in) +
          ((prev != null) ? prev.getGenerationStamp() : 0);
      ret[i] = new Block(id, sz, gs);
      prev = ret[i];
    }
    return ret;
  }

  public static void writeCacheDirectiveInfo(DataOutputStream out,
      CacheDirectiveInfo directive) throws IOException {
    writeLong(directive.getId(), out);
    int flags =
        ((directive.getPath() != null) ? 0x1 : 0) |
        ((directive.getReplication() != null) ? 0x2 : 0) |
        ((directive.getPool() != null) ? 0x4 : 0) |
        ((directive.getExpiration() != null) ? 0x8 : 0);
    out.writeInt(flags);
    if (directive.getPath() != null) {
      writeString(directive.getPath().toUri().getPath(), out);
    }
    if (directive.getReplication() != null) {
      writeShort(directive.getReplication(), out);
    }
    if (directive.getPool() != null) {
      writeString(directive.getPool(), out);
    }
    if (directive.getExpiration() != null) {
      writeLong(directive.getExpiration().getMillis(), out);
    }
  }

  public static CacheDirectiveInfo readCacheDirectiveInfo(DataInput in)
      throws IOException {
    CacheDirectiveInfo.Builder builder =
        new CacheDirectiveInfo.Builder();
    builder.setId(readLong(in));
    int flags = in.readInt();
    if ((flags & 0x1) != 0) {
      builder.setPath(new Path(readString(in)));
    }
    if ((flags & 0x2) != 0) {
      builder.setReplication(readShort(in));
    }
    if ((flags & 0x4) != 0) {
      builder.setPool(readString(in));
    }
    if ((flags & 0x8) != 0) {
      builder.setExpiration(
          CacheDirectiveInfo.Expiration.newAbsolute(readLong(in)));
    }
    if ((flags & ~0xF) != 0) {
      throw new IOException("unknown flags set in " +
          "ModifyCacheDirectiveInfoOp: " + flags);
    }
    return builder.build();
  }

  public static CacheDirectiveInfo readCacheDirectiveInfo(Stanza st)
      throws InvalidXmlException {
    CacheDirectiveInfo.Builder builder =
        new CacheDirectiveInfo.Builder();
    builder.setId(Long.parseLong(st.getValue("ID")));
    String path = st.getValueOrNull("PATH");
    if (path != null) {
      builder.setPath(new Path(path));
    }
    String replicationString = st.getValueOrNull("REPLICATION");
    if (replicationString != null) {
      builder.setReplication(Short.parseShort(replicationString));
    }
    String pool = st.getValueOrNull("POOL");
    if (pool != null) {
      builder.setPool(pool);
    }
    String expiryTime = st.getValueOrNull("EXPIRATION");
    if (expiryTime != null) {
      builder.setExpiration(CacheDirectiveInfo.Expiration.newAbsolute(
          Long.parseLong(expiryTime)));
    }
    return builder.build();
  }

  public static void writeCacheDirectiveInfo(ContentHandler contentHandler,
      CacheDirectiveInfo directive) throws SAXException {
    XMLUtils.addSaxString(contentHandler, "ID",
        Long.toString(directive.getId()));
    if (directive.getPath() != null) {
      XMLUtils.addSaxString(contentHandler, "PATH",
          directive.getPath().toUri().getPath());
    }
    if (directive.getReplication() != null) {
      XMLUtils.addSaxString(contentHandler, "REPLICATION",
          Short.toString(directive.getReplication()));
    }
    if (directive.getPool() != null) {
      XMLUtils.addSaxString(contentHandler, "POOL", directive.getPool());
    }
    if (directive.getExpiration() != null) {
      XMLUtils.addSaxString(contentHandler, "EXPIRATION",
          "" + directive.getExpiration().getMillis());
    }
  }

  public static void writeCachePoolInfo(DataOutputStream out, CachePoolInfo info)
      throws IOException {
    writeString(info.getPoolName(), out);

    final String ownerName = info.getOwnerName();
    final String groupName = info.getGroupName();
    final Long limit = info.getLimit();
    final FsPermission mode = info.getMode();
    final Long maxRelativeExpiry = info.getMaxRelativeExpiryMs();

    boolean hasOwner, hasGroup, hasMode, hasLimit, hasMaxRelativeExpiry;
    hasOwner = ownerName != null;
    hasGroup = groupName != null;
    hasMode = mode != null;
    hasLimit = limit != null;
    hasMaxRelativeExpiry = maxRelativeExpiry != null;

    int flags =
        (hasOwner ? 0x1 : 0) |
        (hasGroup ? 0x2 : 0) |
        (hasMode  ? 0x4 : 0) |
        (hasLimit ? 0x8 : 0) |
        (hasMaxRelativeExpiry ? 0x10 : 0);

    writeInt(flags, out);

    if (hasOwner) {
      writeString(ownerName, out);
    }
    if (hasGroup) {
      writeString(groupName, out);
    }
    if (hasMode) {
      mode.write(out);
    }
    if (hasLimit) {
      writeLong(limit, out);
    }
    if (hasMaxRelativeExpiry) {
      writeLong(maxRelativeExpiry, out);
    }
  }

  public static CachePoolInfo readCachePoolInfo(DataInput in)
      throws IOException {
    String poolName = readString(in);
    CachePoolInfo info = new CachePoolInfo(poolName);
    int flags = readInt(in);
    if ((flags & 0x1) != 0) {
      info.setOwnerName(readString(in));
    }
    if ((flags & 0x2) != 0)  {
      info.setGroupName(readString(in));
    }
    if ((flags & 0x4) != 0) {
      info.setMode(FsPermission.read(in));
    }
    if ((flags & 0x8) != 0) {
      info.setLimit(readLong(in));
    }
    if ((flags & 0x10) != 0) {
      info.setMaxRelativeExpiryMs(readLong(in));
    }
    if ((flags & ~0x1F) != 0) {
      throw new IOException("Unknown flag in CachePoolInfo: " + flags);
    }
    return info;
  }

  public static void writeCachePoolInfo(ContentHandler contentHandler,
      CachePoolInfo info) throws SAXException {
    XMLUtils.addSaxString(contentHandler, "POOLNAME", info.getPoolName());

    final String ownerName = info.getOwnerName();
    final String groupName = info.getGroupName();
    final Long limit = info.getLimit();
    final FsPermission mode = info.getMode();
    final Long maxRelativeExpiry = info.getMaxRelativeExpiryMs();

    if (ownerName != null) {
      XMLUtils.addSaxString(contentHandler, "OWNERNAME", ownerName);
    }
    if (groupName != null) {
      XMLUtils.addSaxString(contentHandler, "GROUPNAME", groupName);
    }
    if (mode != null) {
      FSEditLogOp.fsPermissionToXml(contentHandler, mode);
    }
    if (limit != null) {
      XMLUtils.addSaxString(contentHandler, "LIMIT",
          Long.toString(limit));
    }
    if (maxRelativeExpiry != null) {
      XMLUtils.addSaxString(contentHandler, "MAXRELATIVEEXPIRY",
          Long.toString(maxRelativeExpiry));
    }
  }

  public static CachePoolInfo readCachePoolInfo(Stanza st)
      throws InvalidXmlException {
    String poolName = st.getValue("POOLNAME");
    CachePoolInfo info = new CachePoolInfo(poolName);
    if (st.hasChildren("OWNERNAME")) {
      info.setOwnerName(st.getValue("OWNERNAME"));
    }
    if (st.hasChildren("GROUPNAME")) {
      info.setGroupName(st.getValue("GROUPNAME"));
    }
    if (st.hasChildren("MODE")) {
      info.setMode(FSEditLogOp.fsPermissionFromXml(st));
    }
    if (st.hasChildren("LIMIT")) {
      info.setLimit(Long.parseLong(st.getValue("LIMIT")));
    }
    if (st.hasChildren("MAXRELATIVEEXPIRY")) {
      info.setMaxRelativeExpiryMs(
          Long.parseLong(st.getValue("MAXRELATIVEEXPIRY")));
    }
    return info;
  }

}
