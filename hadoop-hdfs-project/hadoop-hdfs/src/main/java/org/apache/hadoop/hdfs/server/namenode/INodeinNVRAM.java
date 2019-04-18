package org.apache.hadoop.hdfs.server.namenode;

import static org.apache.hadoop.hdfs.server.namenode.snapshot.Snapshot.CURRENT_STATE_ID;

import java.io.FileNotFoundException;
import java.io.IOException;
import java.io.Serializable;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

import org.apache.hadoop.fs.permission.FsPermission;
import org.apache.hadoop.fs.permission.PermissionStatus;
import org.apache.hadoop.hdfs.DFSUtil;
import org.apache.hadoop.hdfs.protocol.Block;
import org.apache.hadoop.hdfs.protocol.QuotaExceededException;
import org.apache.hadoop.hdfs.server.blockmanagement.BlockCollection;
import org.apache.hadoop.hdfs.server.blockmanagement.BlockInfoContiguous;
import org.apache.hadoop.hdfs.server.blockmanagement.BlockInfoContiguousUnderConstruction;
import org.apache.hadoop.hdfs.server.blockmanagement.BlockStoragePolicySuite;
import org.apache.hadoop.hdfs.server.blockmanagement.DatanodeStorageInfo;
import org.apache.hadoop.hdfs.server.namenode.INode.BlocksMapUpdateInfo;
import org.apache.hadoop.hdfs.server.namenode.INode.Feature;
import org.apache.hadoop.hdfs.server.namenode.INodeWithAdditionalFields.PermissionStatusFormat;
import org.apache.hadoop.hdfs.server.namenode.snapshot.DirectoryWithSnapshotFeature;
import org.apache.hadoop.hdfs.server.namenode.snapshot.FileWithSnapshotFeature;
import org.apache.hadoop.hdfs.server.namenode.snapshot.Snapshot;
import org.apache.hadoop.hdfs.util.LongBitFormat;
import org.apache.hadoop.hdfs.util.ReadOnlyList;
import org.apache.hadoop.io.nativeio.NativeIO;
import org.apache.hadoop.io.nativeio.NativeIOException;
import org.apache.hadoop.util.LightWeightGSet.LinkedElement;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

public class INodeinNVRAM extends INode
    implements LinkedElement, BlockCollection {
	static final Logger LOG = LoggerFactory.getLogger(INodeinNVRAM.class);
	private static final long serialVersionUID = 1L;
	public byte[] name = null;
	public int location = 0;
	public List<INode> children = null;

  public INodeinNVRAM(INode parent, byte[] name, int location) {
	  super(parent);
	  this.name = name;
	  this.location = location;
   }

  public void setLocation(int location) {
	  this.location = location;
  }
  
  public int getLocation() {
	  return this.location;
  }
   
  public boolean isFile() {
	    try {
			return (NativeIO.readIntTest(FSDirectory.nvramAddress, location + 8) == 0);
		} catch (NativeIOException e) {
			e.printStackTrace();
			return false;
		}
	  }

	  /** Cast this inode to an {@link INodeFile}.  */
	public INodeFile asFile() {
		INodeFile file = null;
		try {
		int commit = NativeIO.readIntTest(FSDirectory.nvramAddress, location + 4092);
		if (commit == 0) {
			return null;
		}
			file = FSImageSerialization.readINodeFile(location, 12);
		} catch (IOException e) {
			LOG.info("IOException");
		}
		return file;
	}

	public static INodeFile valueOf(INode inode, String path, boolean acceptNull)
	     throws FileNotFoundException, NativeIOException {
	   if (inode == null) {
	     if (acceptNull) {
	       return null;
	     } else {
	       throw new FileNotFoundException("File does not exist: " + path);
	     }
	   }
	   if (!inode.isFile()) {
	     throw new FileNotFoundException("Path is not a file: " + path);
	   }
	   return inode.asFile();
	 }

	  /**
	   * Check whether it's a directory
	 * @throws NativeIOException 
	   */
	public boolean isDirectory() {
		try {
			return (NativeIO.readIntTest(FSDirectory.nvramAddress, location + 8) == 1);
		} catch (NativeIOException e) {
			// TODO Auto-generated catch block
			e.printStackTrace();
			return false;
		}
	  }

	  /** Cast this inode to an {@link INodeDirectory}.  */
	public INodeDirectory asDirectory() {
		
		INodeDirectory dir = null;
		try {
		int commit = NativeIO.readIntTest(FSDirectory.nvramAddress, location + 4092);
		if (commit == 0) {
			return null;
		}
			dir = FSImageSerialization.readINodeDir(location, 12);
		} catch (IOException e) {
			LOG.info("IOException");
		}	
		return dir;
	}
	
	
	public boolean addChildNVRAM(INode node, final boolean setModTime,
		      final int latestSnapshotId) throws QuotaExceededException {
		  final int low = (children == null? -1: Collections.binarySearch(children, node.getLocalNameBytes()));
		    if (low >= 0) {
		      return false;
		    }
		    addChildNVRAM(node, low);
		    if (setModTime) {
		      // update modification time of the parent directory
		      updateModificationTime(node.getModificationTime(), latestSnapshotId);
		    }
		    return true;
	 }
	
	private void addChildNVRAM(final INode node, final int insertionPoint) {
		  if (children == null) {
				children = new ArrayList<INode>(5);
			}
			((INodeinNVRAM)node).setParent(this);
			children.add(-insertionPoint - 1, node);
	}
	
	public boolean addChildDir(INode node, final boolean setModTime,
		      final int latestSnapshotId, long id, PermissionStatus permissions, long modTime, FSDirectory fsd) throws QuotaExceededException {

		    final int low = (children == null? -1: Collections.binarySearch(children, node.getLocalNameBytes()));
		    if (low >= 0) {
		      return false;
		    }
		    addChildDir(node, low, id, permissions, modTime,fsd);
		    if (setModTime) {
		      // update modification time of the parent directory
		      updateModificationTime(node.getModificationTime(), latestSnapshotId);
		    }
		    return true;
	 }
	
	private void addChildDir(final INode node, final int insertionPoint, long id, PermissionStatus permissions,
			long modTime, FSDirectory fsd) {
			int last_position = 0;
			int commit = 1;

			try {			
				fsd.numINode = fsd.numINode + 1;
				int new_offset = 4096 + 4096 * (fsd.numINode - 1);
				((INodeinNVRAM)node).setLocation(new_offset);

				int GRANUL_NVRAM = 1610612736;
				int nvramAddrIdx = new_offset / (4096 + GRANUL_NVRAM); 
				if (FSDirectory.nvramAddress[nvramAddrIdx] == 0) {
					LOG.info("nvramAddrIdx : " + nvramAddrIdx);
					FSDirectory.nvramAddress[nvramAddrIdx] = NativeIO.ReturnNVRAMAddress(GRANUL_NVRAM, 4096 + (GRANUL_NVRAM * nvramAddrIdx));
				}			

				int position = 0;
				long parent_id = NativeIO.readLongTest(FSDirectory.nvramAddress, location);
				// put parent_id : 0 byte --- 8 byte
				position = NativeIO.putLongTest(FSDirectory.nvramAddress, parent_id, new_offset);
				
				// calculate permission
				long permission = PermissionStatusFormat.toLong(permissions);
				final FsPermission p = new FsPermission((short) 0);;
				
				p.fromShort(PermissionStatusFormat.getMode(permission));
				byte[] userName = PermissionStatusFormat.getUser(permission).getBytes();
				
				byte[] groupName = null;
				if(PermissionStatusFormat.getGroup(permission) == null) {
					groupName = this.getGroupName().getBytes();
				} else {
					groupName = PermissionStatusFormat.getGroup(permission).getBytes();
				}
				
				int s = (p.getStickyBit() ? 1 << 9 : 0) | (p.getUserAction().ordinal() << 6)
						| (p.getGroupAction().ordinal() << 3) | p.getOtherAction().ordinal();
							
					
				position = NativeIO.putIntTest(FSDirectory.nvramAddress, 1, position);
				
				position = NativeIO.putIntBATest(FSDirectory.nvramAddress, name.length, name, position);
				position = NativeIO.putLongTest(FSDirectory.nvramAddress, id, position);
				position = NativeIO.putLongTest(FSDirectory.nvramAddress, modTime, position + 8);
					
				
				// put User Name : 968 byte  ---- 1272 byte
				position = NativeIO.putIntPermTest(FSDirectory.nvramAddress,
						userName.length, 
						userName, position);
				// put Group Name : 1272 byte ---- 1576 byte
				position = NativeIO.putIntPermTest(FSDirectory.nvramAddress,
						groupName.length,
						groupName, position);
				
				// put permission : 1576 byte -- 1580 byte
				position = NativeIO.putIntTest(FSDirectory.nvramAddress, s, position);
							
				// put commit byte 4096 - 4
				position = NativeIO.putIntTest(FSDirectory.nvramAddress, commit, new_offset + 4092);
			
			} catch (IOException e) {
				LOG.info("ERROR = " + e.toString() + "Message =" + e.getMessage());
			}

			if (children == null) {
				children = new ArrayList<INode>(5);
			}
			((INodeinNVRAM)node).setParent(this);
			children.add(-insertionPoint - 1, node);

	}
	
	public boolean addChildFile(INode node, final boolean setModTime,
		      final int latestSnapshotId, long id, PermissionStatus permissions, long modTime, short replication, long preferredBlockSize,
			  String clientName, String clientMachine, FSDirectory fsd) throws QuotaExceededException {

		    final int low = (children == null? -1: Collections.binarySearch(children, node.getLocalNameBytes()));
		    if (low >= 0) {
		      return false;
		    }
		    addChildFile(node, low, id, permissions, modTime, replication, preferredBlockSize, clientName, clientMachine, fsd);
		    if (setModTime) {
		      // update modification time of the parent directory
		      updateModificationTime(node.getModificationTime(), latestSnapshotId);
		    }
		    return true;
	 }
	
	private void addChildFile(final INode node, final int insertionPoint, long id, PermissionStatus permissions, long modTime,
			short replication, long preferredBlockSize, String clientName, String clientMachine, FSDirectory fsd) {
			int last_position = 0;
			int commit = 1;

			try {
				
				fsd.numINode = fsd.numINode + 1;
				int new_offset = 4096 + 4096 * (fsd.numINode - 1);
				((INodeinNVRAM)node).setLocation(new_offset);

				int GRANUL_NVRAM = 1610612736;
				int nvramAddrIdx = new_offset / (4096 + GRANUL_NVRAM); 
				if (FSDirectory.nvramAddress[nvramAddrIdx] == 0) {
					LOG.info("nvramAddrIdx : " + nvramAddrIdx);
					FSDirectory.nvramAddress[nvramAddrIdx] = NativeIO.ReturnNVRAMAddress(GRANUL_NVRAM, 4096 + (GRANUL_NVRAM * nvramAddrIdx));
				}			

				int position = 0;
				long parent_id = NativeIO.readLongTest(FSDirectory.nvramAddress, location);
				// put parent_id : 0 byte --- 8 byte
				position = NativeIO.putLongTest(FSDirectory.nvramAddress, parent_id, new_offset);
				
				// calculate permission
				long permission = PermissionStatusFormat.toLong(permissions);
				final FsPermission p = new FsPermission((short) 0);;
				
				p.fromShort(PermissionStatusFormat.getMode(permission));
				byte[] userName = PermissionStatusFormat.getUser(permission).getBytes();
				
				byte[] groupName = null;
				if(PermissionStatusFormat.getGroup(permission) == null) {
					groupName = this.getGroupName().getBytes();
				} else {
					groupName = PermissionStatusFormat.getGroup(permission).getBytes();
				}
				
				int s = (p.getStickyBit() ? 1 << 9 : 0) | (p.getUserAction().ordinal() << 6)
						| (p.getGroupAction().ordinal() << 3) | p.getOtherAction().ordinal();
							
			  // put file/dir : 8 byte --- 12 byte < file : 0 / dir : 1 >
				position = NativeIO.putIntTest(FSDirectory.nvramAddress, 0, position);
					
				// put file name and length : 12 byte --- 316 byte
				position = NativeIO.putIntBATest(FSDirectory.nvramAddress, node.getLocalNameBytes().length,
					node.getLocalNameBytes(), position);

				// put id, replication, modTime, accessTime, preferredBlockSize : 316 byte ---356 byte
				position = NativeIO.putLongLongTest(FSDirectory.nvramAddress, id, (long) replication,
					modTime, modTime, preferredBlockSize, position);
					
				// put writeUnderConstruction : 356 byte --- 360 byte
				position = NativeIO.putIntTest(FSDirectory.nvramAddress, 1, position);
					 
				// put client Name and client Machine : 360 byte --- 968 byte
				position = NativeIO.putIntClientTest(FSDirectory.nvramAddress,
						clientName.getBytes().length, clientName.getBytes(), position);	
				position = NativeIO.putIntClientTest(FSDirectory.nvramAddress,
						clientMachine.getBytes().length, clientMachine.getBytes(), position);	
										
				// put User Name : 968 byte  ---- 1272 byte
				position = NativeIO.putIntPermTest(FSDirectory.nvramAddress,
					userName.length, 
					userName, position);
				// put Group Name : 1272 byte ---- 1576 byte
				position = NativeIO.putIntPermTest(FSDirectory.nvramAddress,
					groupName.length,
					groupName, position);
					
				// put permission : 1576 byte -- 1580 byte
				position = NativeIO.putIntTest(FSDirectory.nvramAddress, s, position);

				// put block length 0
				position = NativeIO.putIntTest(FSDirectory.nvramAddress, 0, position);
					
				// put commit byte 4096 - 4
				position = NativeIO.putIntTest(FSDirectory.nvramAddress, commit, new_offset + 4092);
			
			} catch (IOException e) {
				LOG.info("ERROR = " + e.toString() + "Message =" + e.getMessage());
			}

			if (children == null) {
				children = new ArrayList<INode>(5);
			}
			((INodeinNVRAM)node).setParent(this);
			children.add(-insertionPoint - 1, node);

	}
	
	public void addBlockNVRAM(Block blk) {
		int commit = 1;
		if (location == -1) {
			LOG.info("addBlockNVRAM error");
		}
		try {
			int block_num = NativeIO.readIntTest(FSDirectory.nvramAddress, location + 1580);
			block_num = block_num + 1;
			NativeIO.putBlockTest(FSDirectory.nvramAddress,  blk.getBlockId(), blk.getGenerationStamp(),
					location + 1584 + ((block_num - 1) * 24));
			NativeIO.putIntTest(FSDirectory.nvramAddress, block_num, location + 1580);		
		} catch (IOException e) {
			// TODO Auto-generated catch block
			LOG.info("ERROR = " + e.toString() + "Message =" + e.getMessage());
		}
	}
	
  public boolean commitBlock(Block blk) {
		try {
				if (location == -1) {
					LOG.info("commit error");
					return false;
				}
				
				int numblocks = NativeIO.readIntTest(FSDirectory.nvramAddress, location + 1580);
				NativeIO.putLongTest(FSDirectory.nvramAddress, blk.getNumBytes(),
						location + 1584 + ((24 * (numblocks - 1)) + 8));
				return true;
			} catch (NativeIOException e) {
				LOG.info("native IO Exception occur");
				return false;
			}
    }
  
  public INode getChild(byte[] name, int snapshotId) {
     ReadOnlyList<INode> c = (children == null ? ReadOnlyList.Util.<INode> emptyList()
    	        : ReadOnlyList.Util.asReadOnlyList(children));
     final int i = ReadOnlyList.Util.binarySearch(c, name);
     return i < 0 ? null : c.get(i);
    }
  
	public boolean removeChild(INode child, int latestSnapshotId) {
		 INodeinNVRAM target = (INodeinNVRAM)child;
	   final int i = (children == null? -1: Collections.binarySearch(children, target.name));
	   if (i < 0) {
	     return false;
	      }
	   
//		if (!rename) {
//			try {
//				NativeIO.putLongTest(FSDirectory.nvramAddress, 0, target.location + 4092);
//				if (target.isDirectory()) {
//					for (INode child_item : children) {
//						removeChild(child_item, latestSnapshotId, false);
//					}
//				}
//			} catch (NativeIOException e) {
//				// TODO Auto-generated catch block
//				e.printStackTrace();
//			}
//		}
		 children.remove(i);
	   return true;
    }
	
	public short getFileReplication() {
		try {
			return (short)NativeIO.readLongTest(FSDirectory.nvramAddress, location + 324);
		} catch (NativeIOException e) {
			// TODO Auto-generated catch block
			e.printStackTrace();
			return 0;
		}
	}
	
	public long getPreferredBlockSize() {
		try {
			return NativeIO.readLongTest(FSDirectory.nvramAddress, location + 348);
		} catch (NativeIOException e) {
			// TODO Auto-generated catch block
			e.printStackTrace();
			return 0;
		}
	}
	
	public final void setParent(INodeinNVRAM parent) {
		  this.parent = parent;
			try {
				NativeIO.putLongTest(FSDirectory.nvramAddress, parent.location, 0);
			} catch (NativeIOException e) {
				// TODO Auto-generated catch block
				e.printStackTrace();
			}	    
	}
	
  @Override
  public void setNext(LinkedElement next) {

  }
  
  @Override
  public LinkedElement getNext() {
    return null;
  }

  @Override
  final PermissionStatus getPermissionStatus(int snapshotId) {
	    return new PermissionStatus(getUserName(snapshotId), getGroupName(snapshotId),
	            getFsPermission(snapshotId));
  }

  @Override
  final String getUserName(int snapshotId) {
	  long permission = 0; 
		try {
			if(this.isDirectory()) permission = PermissionStatusFormat.toLong(PermissionStatus.read(location, 340, FSDirectory.nvramAddress));
			if(this.isFile()) permission = PermissionStatusFormat.toLong(PermissionStatus.read(location, 968, FSDirectory.nvramAddress));
		} catch (IOException e) {
			// TODO Auto-generated catch block
			e.printStackTrace();
		}
    return PermissionStatusFormat.getUser(permission);
  }

  @Override
  final void setUser(String user) {
  }

  @Override
  final String getGroupName(int snapshotId) {
	  long permission = 0; 
		try {
			if(this.isDirectory()) permission = PermissionStatusFormat.toLong(PermissionStatus.read(location, 340, FSDirectory.nvramAddress));
			if(this.isFile()) permission = PermissionStatusFormat.toLong(PermissionStatus.read(location, 968, FSDirectory.nvramAddress));
		} catch (IOException e) {
			// TODO Auto-generated catch block
			e.printStackTrace();
		}
    return PermissionStatusFormat.getGroup(permission);
  }

  @Override
  final void setGroup(String group) {	  
  }

  @Override
  final FsPermission getFsPermission(int snapshotId) {
	  return new FsPermission(getFsPermissionShort());
  }

  @Override
  public final short getFsPermissionShort() {
	  long permission = 0; 
		try {
			if(this.isDirectory()) permission = PermissionStatusFormat.toLong(PermissionStatus.read(location, 340, FSDirectory.nvramAddress));
			if(this.isFile()) permission = PermissionStatusFormat.toLong(PermissionStatus.read(location, 968, FSDirectory.nvramAddress));
		} catch (IOException e) {
			// TODO Auto-generated catch block
			e.printStackTrace();
		}
    return PermissionStatusFormat.getMode(permission);
  }
  @Override
  void setPermission(FsPermission permission) {
  }

  @Override
  public long getPermissionLong() {
	  long permission = 0; 
		try {
			if(this.isDirectory()) permission = PermissionStatusFormat.toLong(PermissionStatus.read(location, 340, FSDirectory.nvramAddress));
			if(this.isFile()) permission = PermissionStatusFormat.toLong(PermissionStatus.read(location, 968, FSDirectory.nvramAddress));
		} catch (IOException e) {
			// TODO Auto-generated catch block
			e.printStackTrace();
		}
		return permission;
  }

  @Override
  public final AclFeature getAclFeature(int snapshotId) {
	  return null;
  }

  @Override
  final long getModificationTime(int snapshotId) {
		try {
			return NativeIO.readLongTest(FSDirectory.nvramAddress, location + 332);
		} catch (NativeIOException e) {
			// TODO Auto-generated catch block
			e.printStackTrace();
			return 0;
		}
 }


  /** Update modification time if it is larger than the current value. */
  @Override
  public final INode updateModificationTime(long mtime, int latestSnapshotId) {
		try {
			NativeIO.putLongTest(FSDirectory.nvramAddress, mtime, location + 332);
		} catch (NativeIOException e) {
			// TODO Auto-generated catch block
			e.printStackTrace();
		}
	  return this;
  }

  final void cloneModificationTime(INodeWithAdditionalFields that) {
  }

  @Override
  public final void setModificationTime(long modificationTime) {
		try {
			NativeIO.putLongTest(FSDirectory.nvramAddress, modificationTime, location + 332);
		} catch (NativeIOException e) {
			// TODO Auto-generated catch block
			e.printStackTrace();
		}
  }

  @Override
  final long getAccessTime(int snapshotId) {
		try {
			return NativeIO.readLongTest(FSDirectory.nvramAddress, location + 340);
		} catch (NativeIOException e) {
			// TODO Auto-generated catch block
			e.printStackTrace();
			return 0;
		}
  }

  /**
   * Set last access time of inode.
   */
  @Override
  public final void setAccessTime(long accessTime) {
		try {
			NativeIO.putLongTest(FSDirectory.nvramAddress, accessTime, location + 340);
		} catch (NativeIOException e) {
			// TODO Auto-generated catch block
			e.printStackTrace();
		}
 }

  protected void addFeature(Feature f) {
  }

  protected void removeFeature(Feature f) {
  }

  protected <T extends Feature> T getFeature(Class<? extends Feature> clazz) {
	  return null;
  }

  public void removeAclFeature() {
  }

  public void addAclFeature(AclFeature f) {
  }
  
  @Override
  XAttrFeature getXAttrFeature(int snapshotId) {
	  return null;
  }
  
  @Override
  public void removeXAttrFeature() {
  }
  
  @Override
  public void addXAttrFeature(XAttrFeature f) {
  }

  public final Feature[] getFeatures() {
    return null;
  }


@Override
public byte[] getLocalNameBytes() {
	// TODO Auto-generated method stub
	return name;
}


@Override
public long getId() {
	try {
		return NativeIO.readLongTest(FSDirectory.nvramAddress, location + 316);
	} catch (NativeIOException e) {
		// TODO Auto-generated catch block
		e.printStackTrace();
		return 0;
	}			
}


@Override
void recordModification(int latestSnapshotId) {
	// TODO Auto-generated method stub
	
}


@Override
public QuotaCounts cleanSubtree(BlockStoragePolicySuite bsps, int snapshotId, int priorSnapshotId,
		BlocksMapUpdateInfo collectedBlocks, List<INode> removedINodes) {
	// TODO Auto-generated method stub
  return new QuotaCounts.Builder().build();
}


@Override
public void destroyAndCollectBlocks(BlockStoragePolicySuite bsps, BlocksMapUpdateInfo collectedBlocks,
		List<INode> removedINodes) {
	
		try {
			NativeIO.putLongTest(FSDirectory.nvramAddress, 0, this.location + 4092);
			if (this.isDirectory()) {
				if (children != null) {
					for (INode child_item : children) {
						child_item.destroyAndCollectBlocks(bsps, collectedBlocks, removedINodes);
					}
					children.clear();
				}
				clear();
				removedINodes.add(this);

			} else if (this.isFile()) {
				int numBlocks = NativeIO.readIntTest(FSDirectory.nvramAddress, location + 1580);
				BlockInfoContiguous[] blocks = new BlockInfoContiguous[numBlocks];
				Block blk = new Block();
				int new_pos = 1584;
				short replication = this.getFileReplication();

				for (int i = 0; i < numBlocks; i++) {
					blk.setBlockId(NativeIO.readLongTest(FSDirectory.nvramAddress, this.location + new_pos));
					new_pos += 8;
					blk.setNumBytes(NativeIO.readLongTest(FSDirectory.nvramAddress, this.location + new_pos));
					new_pos += 8;
					blk.setGenerationStamp(NativeIO.readLongTest(FSDirectory.nvramAddress, this.location + new_pos));
					new_pos += 8;

					blocks[i] = new BlockInfoContiguous(blk, replication);
				}

				if (blocks != null && collectedBlocks != null) {
					for (BlockInfoContiguous bk : blocks) {
						collectedBlocks.addDeleteBlock(bk);
					}
				}
				clear();
				removedINodes.add(this);
			}
		} catch (NativeIOException e) {
			// TODO Auto-generated catch block
			e.printStackTrace();
		}
	
}


@Override
public ContentSummaryComputationContext computeContentSummary(ContentSummaryComputationContext summary) {
	// TODO Auto-generated method stub
	return null;
}


@Override
public QuotaCounts computeQuotaUsage(BlockStoragePolicySuite bsps, byte blockStoragePolicyId, QuotaCounts counts,
		boolean useCache, int lastSnapshotId) {
	//TODO IMPLEMENTATION
	return counts;
}


@Override
public void setLocalName(byte[] name) {
	// TODO Auto-generated method stub
	this.name = name;
	try {
		NativeIO.putIntBATest(FSDirectory.nvramAddress, name.length,
				name, location + 12);
	} catch (NativeIOException e) {
		// TODO Auto-generated catch block
		e.printStackTrace();
	}
}


@Override
public byte getStoragePolicyID() {
	// TODO Auto-generated method stub
	return 0;
}


@Override
public byte getLocalStoragePolicyID() {
	// TODO Auto-generated method stub
	return 0;
}

@Override
public BlockInfoContiguous getLastBlock() {
		int numBlocks = 0;
		try {
			numBlocks = NativeIO.readIntTest(FSDirectory.nvramAddress, location + 1580);
		} catch (NativeIOException e) {
			e.printStackTrace();
		}

		short replication = this.getFileReplication();
		Block blk = new Block();
		int new_pos = 1584 + 24 * (numBlocks - 1);
		try {
			blk.setBlockId(NativeIO.readLongTest(FSDirectory.nvramAddress, this.location + new_pos));
			new_pos += 8;
			blk.setNumBytes(NativeIO.readLongTest(FSDirectory.nvramAddress, this.location + new_pos));
			new_pos += 8;
			blk.setGenerationStamp(NativeIO.readLongTest(FSDirectory.nvramAddress, this.location + new_pos));
			new_pos += 8;
		} catch (NativeIOException e) {
			e.printStackTrace();
		}
		BlockInfoContiguous bic = new BlockInfoContiguous(blk, replication);
		return bic;
}

@Override
public int numBlocks() {
	int numBlocks = 0;
	try {
		numBlocks = NativeIO.readIntTest(FSDirectory.nvramAddress, location + 1580);
	} catch (NativeIOException e) {
		e.printStackTrace();
	}

	return numBlocks;
}

@Override
public BlockInfoContiguous[] getBlocks() {
		int numBlocks = 0;
		try {
			numBlocks = NativeIO.readIntTest(FSDirectory.nvramAddress, location + 1580);
		} catch (NativeIOException e) {
			e.printStackTrace();
		}

		BlockInfoContiguous[] blocks = new BlockInfoContiguous[numBlocks];
		Block blk = new Block();
		int new_pos = 1584;
		short replication = this.getFileReplication();

		try {
			for (int i = 0; i < numBlocks; i++) {
				blk.setBlockId(NativeIO.readLongTest(FSDirectory.nvramAddress, this.location + new_pos));
				new_pos += 8;
				blk.setNumBytes(NativeIO.readLongTest(FSDirectory.nvramAddress, this.location + new_pos));
				new_pos += 8;
				blk.setGenerationStamp(NativeIO.readLongTest(FSDirectory.nvramAddress, this.location + new_pos));
				new_pos += 8;

				blocks[i] = new BlockInfoContiguous(blk, replication);
			}
		} catch (NativeIOException e) {
			e.printStackTrace();
		}
		// TODO Auto-generated method stub
		return blocks;
}

@Override
public short getBlockReplication() {
	short max = getFileReplication();
	return max;
}

@Override
public String getName() {
	// TODO Auto-generated method stub
	return FSDirectory.getFullPathName(this);
}

@Override
public void setBlock(int index, BlockInfoContiguous blk) {
	// TODO Auto-generated method stub
	
}

@Override
public BlockInfoContiguousUnderConstruction setLastBlock(BlockInfoContiguous lastBlock, DatanodeStorageInfo[] targets)
		throws IOException {
	// TODO Auto-generated method stub
	return null;
}

@Override
public boolean isUnderConstruction() {
		int isUnderConstruction = 0;
		try {
			isUnderConstruction = NativeIO.readIntTest(FSDirectory.nvramAddress, location + 356);
		} catch (NativeIOException e) {
			e.printStackTrace();
		}

		return isUnderConstruction == 1;
}

public long computeFileSize(int snapshotId) {
		int numBlocks = 0;
		try {
			numBlocks = NativeIO.readIntTest(FSDirectory.nvramAddress, location + 1580);
		} catch (NativeIOException e) {
			e.printStackTrace();
		}

		int new_pos = 1584 + 8;
		long size = 0;

		try {
			for (int i = 0; i < numBlocks; i++) {
				size += NativeIO.readLongTest(FSDirectory.nvramAddress, this.location + new_pos);
				new_pos += 24;
			}
		} catch (NativeIOException e) {
			e.printStackTrace();
		}

		return size;
	}
}
