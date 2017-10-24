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

import java.io.ByteArrayOutputStream;
import java.io.FileNotFoundException;
import java.io.IOException;
import java.io.PrintStream;
import java.io.PrintWriter;
import java.io.Serializable;
import java.nio.Buffer;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.Iterator;
import java.util.LinkedList;
import java.util.List;
import java.util.Map;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import org.apache.hadoop.fs.PathIsNotDirectoryException;
import org.apache.hadoop.fs.permission.PermissionStatus;
import org.apache.hadoop.fs.StorageType;
import org.apache.hadoop.fs.XAttr;
import org.apache.hadoop.hdfs.DFSUtil;
import org.apache.hadoop.hdfs.protocol.QuotaExceededException;
import org.apache.hadoop.hdfs.protocol.SnapshotException;
import org.apache.hadoop.hdfs.server.blockmanagement.BlockCollection;
import org.apache.hadoop.hdfs.server.blockmanagement.BlockInfoContiguous;
import org.apache.hadoop.hdfs.server.blockmanagement.BlockStoragePolicySuite;
import org.apache.hadoop.hdfs.server.blockmanagement.DatanodeStorageInfo;
import org.apache.hadoop.hdfs.server.namenode.INodeReference.WithCount;
import org.apache.hadoop.hdfs.server.namenode.snapshot.DirectorySnapshottableFeature;
import org.apache.hadoop.hdfs.server.namenode.snapshot.DirectoryWithSnapshotFeature;
import org.apache.hadoop.hdfs.server.namenode.snapshot.DirectoryWithSnapshotFeature.DirectoryDiffList;
import org.apache.hadoop.hdfs.server.namenode.snapshot.Snapshot;
import org.apache.hadoop.hdfs.util.Diff.ListType;
import org.apache.hadoop.hdfs.util.ReadOnlyList;
import org.apache.hadoop.io.nativeio.NativeIO;
import org.apache.hadoop.io.nativeio.NativeIOException;
import org.apache.hadoop.util.BytesUtil;
import org.apache.commons.logging.Log;
import org.apache.commons.logging.LogFactory;
import org.apache.commons.logging.impl.Log4JLogger;

import com.google.common.annotations.VisibleForTesting;
import com.google.common.base.Preconditions;
import com.google.common.collect.ImmutableList;

import static org.apache.hadoop.hdfs.server.blockmanagement.BlockStoragePolicySuite.ID_UNSPECIFIED;

/**
 * Directory INode class.
 */
public class INodeDirectory extends INodeWithAdditionalFields
    implements INodeDirectoryAttributes, Serializable {
	  /**
	 * 
	 */
	private static final long serialVersionUID = -2588820578442414956L;
	static final Log LOG = LogFactory.getLog(INodeDirectory.class);
  /** Cast INode to INodeDirectory. */
  public static INodeDirectory valueOf(INode inode, Object path
      ) throws FileNotFoundException, PathIsNotDirectoryException {
    if (inode == null) {
      throw new FileNotFoundException("Directory does not exist: "
          + DFSUtil.path2String(path));
    }
    if (!inode.isDirectory()) {
      throw new PathIsNotDirectoryException(DFSUtil.path2String(path));
    }
    return inode.asDirectory(); 
  }

  protected static final int DEFAULT_FILES_PER_DIRECTORY = 5;
  final static byte[] ROOT_NAME = DFSUtil.string2Bytes("");

  private List<INode> children = null;
  public transient ByteBuffer children_for_nvram = null;
  public int offset;
  public int pos;
  public int nvram_location;
  
  /** constructor */
  public INodeDirectory(long id, byte[] name, PermissionStatus permissions,
      long mtime) {
    super(id, name, permissions, mtime, 0L);
  }
  
  /**
   * Copy constructor
   * @param other The INodeDirectory to be copied
   * @param adopt Indicate whether or not need to set the parent field of child
   *              INodes to the new node
   * @param featuresToCopy any number of features to copy to the new node.
   *              The method will do a reference copy, not a deep copy.
   */
  public INodeDirectory(INodeDirectory other, boolean adopt,
      Feature... featuresToCopy) {
    super(other);
    this.children = other.children;
    if (adopt && this.children != null) {
      for (INode child : children) {
        child.setParent(this);
      }
    }
    this.features = featuresToCopy;
    AclFeature aclFeature = getFeature(AclFeature.class);
    if (aclFeature != null) {
      // for the de-duplication of AclFeature
      removeFeature(aclFeature);
      addFeature(AclStorage.addAclFeature(aclFeature));
    }
  }

  /** @return true unconditionally. */
  @Override
  public final boolean isDirectory() {
    return true;
  }

  /** @return this object. */
  @Override
  public final INodeDirectory asDirectory() {
    return this;
  }

  @Override
  public byte getLocalStoragePolicyID() {
    XAttrFeature f = getXAttrFeature();
    ImmutableList<XAttr> xattrs = f == null ? ImmutableList.<XAttr> of() : f
        .getXAttrs();
    for (XAttr xattr : xattrs) {
      if (BlockStoragePolicySuite.isStoragePolicyXAttr(xattr)) {
        return (xattr.getValue())[0];
      }
    }
    return ID_UNSPECIFIED;
  }

  @Override
  public byte getStoragePolicyID() {
    byte id = getLocalStoragePolicyID();
    if (id != ID_UNSPECIFIED) {
      return id;
    }
    // if it is unspecified, check its parent
    return getParent() != null ? getParent().getStoragePolicyID() :
        ID_UNSPECIFIED;
  }

  void setQuota(BlockStoragePolicySuite bsps, long nsQuota, long ssQuota, StorageType type) {
    DirectoryWithQuotaFeature quota = getDirectoryWithQuotaFeature();
    if (quota != null) {
      // already has quota; so set the quota to the new values
      if (type != null) {
        quota.setQuota(ssQuota, type);
      } else {
        quota.setQuota(nsQuota, ssQuota);
      }
      if (!isQuotaSet() && !isRoot()) {
        removeFeature(quota);
      }
    } else {
      final QuotaCounts c = computeQuotaUsage(bsps);
      DirectoryWithQuotaFeature.Builder builder =
          new DirectoryWithQuotaFeature.Builder().nameSpaceQuota(nsQuota);
      if (type != null) {
        builder.typeQuota(type, ssQuota);
      } else {
        builder.storageSpaceQuota(ssQuota);
      }
      addDirectoryWithQuotaFeature(builder.build()).setSpaceConsumed(c);
    }
  }

  @Override
  public QuotaCounts getQuotaCounts() {
    final DirectoryWithQuotaFeature q = getDirectoryWithQuotaFeature();
    return q != null? q.getQuota(): super.getQuotaCounts();
  }

  @Override
  public void addSpaceConsumed(QuotaCounts counts, boolean verify)
    throws QuotaExceededException {
    final DirectoryWithQuotaFeature q = getDirectoryWithQuotaFeature();
    if (q != null) {
      q.addSpaceConsumed(this, counts, verify);
    } else {
      addSpaceConsumed2Parent(counts, verify);
    }
  }

  /**
   * If the directory contains a {@link DirectoryWithQuotaFeature}, return it;
   * otherwise, return null.
   */
  public final DirectoryWithQuotaFeature getDirectoryWithQuotaFeature() {
    return getFeature(DirectoryWithQuotaFeature.class);
  }

  /** Is this directory with quota? */
  final boolean isWithQuota() {
    return getDirectoryWithQuotaFeature() != null;
  }

  DirectoryWithQuotaFeature addDirectoryWithQuotaFeature(
      DirectoryWithQuotaFeature q) {
    Preconditions.checkState(!isWithQuota(), "Directory is already with quota");
    addFeature(q);
    return q;
  }

  int searchChildren(byte[] name) {
    return children == null? -1: Collections.binarySearch(children, name);
  }
  
  int searchChildren(byte[] name, boolean nvram_enabled) {
//	 TODO: NVRAM INODE is stored incrementally by time
   if (nvram_enabled == true) {
//	   int inode_num = 0;
//	   int lastposition = 0;
//	   int index = 0;
//	   
//	   if(children_for_nvram == null) {
//		   return -1;
//	   }
//	   inode_num = children_for_nvram.getInt();
//	   lastposition = children_for_nvram.getInt();
//	   
//	   while (children_for_nvram.position() < lastposition) {
//		   index++;
//		   int size = children_for_nvram.getInt();
//		   byte[] byte_for_inode = new byte[size];
//		   children_for_nvram.get(byte_for_inode);
//		   byte commit = children_for_nvram.get();
//		   try {
//			   INode inode = (INode) BytesUtil.toObject(byte_for_inode);
//			   if(name.equals(inode.getLocalNameBytes())) {
//				   if(commit == 1) {
//					   return index; 
//				   } else	{
//					   return -1;
//				   }
//			   }
//			} catch ( ClassNotFoundException | IOException e) {
//			   e.printStackTrace();
//		   }
//		   
//	   }
	  } else {
    return children == null? -1: Collections.binarySearch(children, name);
	  }
   return -1;
  }
  
  public DirectoryWithSnapshotFeature addSnapshotFeature(
      DirectoryDiffList diffs) {
    Preconditions.checkState(!isWithSnapshot(), 
        "Directory is already with snapshot");
    DirectoryWithSnapshotFeature sf = new DirectoryWithSnapshotFeature(diffs);
    addFeature(sf);
    return sf;
  }
  
  /**
   * If feature list contains a {@link DirectoryWithSnapshotFeature}, return it;
   * otherwise, return null.
   */
  public final DirectoryWithSnapshotFeature getDirectoryWithSnapshotFeature() {
    return getFeature(DirectoryWithSnapshotFeature.class);
  }

  /** Is this file has the snapshot feature? */
  public final boolean isWithSnapshot() {
    return getDirectoryWithSnapshotFeature() != null;
  }

  public DirectoryDiffList getDiffs() {
    DirectoryWithSnapshotFeature sf = getDirectoryWithSnapshotFeature();
    return sf != null ? sf.getDiffs() : null;
  }
  
  @Override
  public INodeDirectoryAttributes getSnapshotINode(int snapshotId) {
    DirectoryWithSnapshotFeature sf = getDirectoryWithSnapshotFeature();
    return sf == null ? this : sf.getDiffs().getSnapshotINode(snapshotId, this);
  }
  
  @Override
  public String toDetailString() {
    DirectoryWithSnapshotFeature sf = this.getDirectoryWithSnapshotFeature();
    return super.toDetailString() + (sf == null ? "" : ", " + sf.getDiffs()); 
  }

  public DirectorySnapshottableFeature getDirectorySnapshottableFeature() {
    return getFeature(DirectorySnapshottableFeature.class);
  }

  public boolean isSnapshottable() {
    return getDirectorySnapshottableFeature() != null;
  }

  public Snapshot getSnapshot(byte[] snapshotName) {
    return getDirectorySnapshottableFeature().getSnapshot(snapshotName);
  }

  public void setSnapshotQuota(int snapshotQuota) {
    getDirectorySnapshottableFeature().setSnapshotQuota(snapshotQuota);
  }
 
  public Snapshot addSnapshot(int id, String name) throws SnapshotException,
      QuotaExceededException {
    return getDirectorySnapshottableFeature().addSnapshot(this, id, name);
  }

  public Snapshot removeSnapshot(BlockStoragePolicySuite bsps, String snapshotName,
      BlocksMapUpdateInfo collectedBlocks, final List<INode> removedINodes)
      throws SnapshotException {
    return getDirectorySnapshottableFeature().removeSnapshot(bsps, this,
        snapshotName, collectedBlocks, removedINodes);
  }

  public void renameSnapshot(String path, String oldName, String newName)
      throws SnapshotException {
    getDirectorySnapshottableFeature().renameSnapshot(path, oldName, newName);
  }

  /** add DirectorySnapshottableFeature */
  public void addSnapshottableFeature() {
    Preconditions.checkState(!isSnapshottable(),
        "this is already snapshottable, this=%s", this);
    DirectoryWithSnapshotFeature s = this.getDirectoryWithSnapshotFeature();
    final DirectorySnapshottableFeature snapshottable =
        new DirectorySnapshottableFeature(s);
    if (s != null) {
      this.removeFeature(s);
    }
    this.addFeature(snapshottable);
  }

  /** remove DirectorySnapshottableFeature */
  public void removeSnapshottableFeature() {
    DirectorySnapshottableFeature s = getDirectorySnapshottableFeature();
    Preconditions.checkState(s != null,
        "The dir does not have snapshottable feature: this=%s", this);
    this.removeFeature(s);
    if (s.getDiffs().asList().size() > 0) {
      // add a DirectoryWithSnapshotFeature back
      DirectoryWithSnapshotFeature sf = new DirectoryWithSnapshotFeature(
          s.getDiffs());
      addFeature(sf);
    }
  }

  /** 
   * Replace the given child with a new child. Note that we no longer need to
   * replace an normal INodeDirectory or INodeFile into an
   * INodeDirectoryWithSnapshot or INodeFileUnderConstruction. The only cases
   * for child replacement is for reference nodes.
   */
  public void replaceChild(INode oldChild, final INode newChild,
      final INodeMap inodeMap) {
    Preconditions.checkNotNull(children);
    final int i = searchChildren(newChild.getLocalNameBytes());
    Preconditions.checkState(i >= 0);
    Preconditions.checkState(oldChild == children.get(i)
        || oldChild == children.get(i).asReference().getReferredINode()
            .asReference().getReferredINode());
    oldChild = children.get(i);
    
    if (oldChild.isReference() && newChild.isReference()) {
      // both are reference nodes, e.g., DstReference -> WithName
      final INodeReference.WithCount withCount = 
          (WithCount) oldChild.asReference().getReferredINode();
      withCount.removeReference(oldChild.asReference());
    }
    children.set(i, newChild);
    
    // replace the instance in the created list of the diff list
    DirectoryWithSnapshotFeature sf = this.getDirectoryWithSnapshotFeature();
    if (sf != null) {
      sf.getDiffs().replaceChild(ListType.CREATED, oldChild, newChild);
    }
    
    // update the inodeMap
    if (inodeMap != null) {
      inodeMap.put(newChild);
    }    
  }

  INodeReference.WithName replaceChild4ReferenceWithName(INode oldChild,
      int latestSnapshotId) {
    Preconditions.checkArgument(latestSnapshotId != Snapshot.CURRENT_STATE_ID);
    if (oldChild instanceof INodeReference.WithName) {
      return (INodeReference.WithName)oldChild;
    }

    final INodeReference.WithCount withCount;
    if (oldChild.isReference()) {
      Preconditions.checkState(oldChild instanceof INodeReference.DstReference);
      withCount = (INodeReference.WithCount) oldChild.asReference()
          .getReferredINode();
    } else {
      withCount = new INodeReference.WithCount(null, oldChild);
    }
    final INodeReference.WithName ref = new INodeReference.WithName(this,
        withCount, oldChild.getLocalNameBytes(), latestSnapshotId);
    replaceChild(oldChild, ref, null);
    return ref;
  }

  @Override
  public void recordModification(int latestSnapshotId) {
    if (isInLatestSnapshot(latestSnapshotId)
        && !shouldRecordInSrcSnapshot(latestSnapshotId)) {
      // add snapshot feature if necessary
      DirectoryWithSnapshotFeature sf = getDirectoryWithSnapshotFeature();
      if (sf == null) {
        sf = addSnapshotFeature(null);
      }
      // record self in the diff list if necessary
      sf.getDiffs().saveSelf2Snapshot(latestSnapshotId, this, null);
    }
  }

  /**
   * Save the child to the latest snapshot.
   * 
   * @return the child inode, which may be replaced.
 * @throws IOException 
 * @throws ClassNotFoundException 
   */
  public INode saveChild2Snapshot(final INode child, final int latestSnapshotId,
      final INode snapshotCopy) {
    if (latestSnapshotId == Snapshot.CURRENT_STATE_ID) {
      return child;
    }
    
    // add snapshot feature if necessary
    DirectoryWithSnapshotFeature sf = getDirectoryWithSnapshotFeature();
    if (sf == null) {
      sf = this.addSnapshotFeature(null);
    }
    return sf.saveChild2Snapshot(this, child, latestSnapshotId, snapshotCopy);
  }

  /**
   * @param name the name of the child
   * @param snapshotId
   *          if it is not {@link Snapshot#CURRENT_STATE_ID}, get the result
   *          from the corresponding snapshot; otherwise, get the result from
   *          the current directory.
   * @return the child inode.
 * @throws IOException 
   */
	public INode getChild(byte[] name, int snapshotId, boolean nvram_enabled, int location) {
		if (nvram_enabled == true) {
			LOG.info("trying to find = " + new String(name));
			//int index = 0;
			try {
				if(location == -1)
				{
					return null;
				}
				int pos = 0;
				int new_offset = location;
				LOG.info("new_offset = " + new_offset);
				int root = NativeIO.readIntFromNVRAM(4096, new_offset, pos);
				pos = pos + 4;
				int file_dir = NativeIO.readIntFromNVRAM(4096, new_offset, pos);
				pos = pos + 4;

				INode inode = null;
				if (file_dir == 0) {
					try {
						INodeFile file = FSImageSerialization.readINodeFile(new_offset, pos);
						pos = file.pos;
						file.setParent(this);
						inode = (INode) file;
					} catch (IOException e) {
						LOG.info("IOException");
					}
				} else if (file_dir == 1) {
					try {
						INodeDirectory dir = FSImageSerialization.readINodeDir(new_offset, pos);
						pos = dir.pos;
						dir.setParent(this);
						inode = (INode) dir;
					} catch (IOException e) {
						LOG.info("IOException");
					}
				} else {
					return null;
				}

				int commit = NativeIO.readIntFromNVRAM(4096, new_offset, pos);
				pos = pos + 4;

				if (inode == null)
					return null;
				LOG.info("found name = " + inode.getLocalName());
				if (commit == 1) {
					return inode;
				} else {
					return null;
				}
				
				/**
				int inode_num = NativeIO.readIntFromNVRAM(4096, 4096, 0);
				LOG.info("NativeIO inode_num " + inode_num);
				if (inode_num == 0)
					return null;

				while (index < inode_num) {
					int pos = 0;
					int new_offset = 8192 + (index * 4096);
					LOG.info("new_offset = " + new_offset);
					int root = NativeIO.readIntFromNVRAM(4096, new_offset, pos);
					pos = pos + 4;
					int file_dir = NativeIO.readIntFromNVRAM(4096, new_offset, pos);
					pos = pos + 4;

					INode inode = null;
					if (file_dir == 0) {
						try {
							INodeFile file = FSImageSerialization.readINodeFile(new_offset, pos);
							pos = file.pos;
							file.setParent(this);
							inode = (INode) file;
						} catch (IOException e) {
							LOG.info("IOException");
						}
					} else if (file_dir == 1) {
						try {
							INodeDirectory dir = FSImageSerialization.readINodeDir(new_offset, pos);
							pos = dir.pos;
							dir.setParent(this);
							inode = (INode) dir;
						} catch (IOException e) {
							LOG.info("IOException");
						}
					} else {
						return null;
					}

					int commit = NativeIO.readIntFromNVRAM(4096, new_offset, pos);
					pos = pos + 4;

					if (inode == null)
						return null;

					LOG.info("found name = " + inode.getLocalName());
					if (Arrays.equals(name, inode.getLocalNameBytes())) {
						if (commit == 1) {
							return inode;
						} else {
							index = index + 1;
						}
					} else {
						index = index + 1;
					}
				}**/
			} catch (NativeIOException e) {
				LOG.info("native IO Exception occur");
			}

		} else {
			DirectoryWithSnapshotFeature sf;
			if (snapshotId == Snapshot.CURRENT_STATE_ID || (sf = getDirectoryWithSnapshotFeature()) == null) {
				ReadOnlyList<INode> c = getCurrentChildrenList();
				final int i = ReadOnlyList.Util.binarySearch(c, name);
				return i < 0 ? null : c.get(i);
			}

			return sf.getChild(this, name, snapshotId, nvram_enabled, location);
		}
		return null;
	}
  
  public INode getChild(byte[] name, int snapshotId) {
	    DirectoryWithSnapshotFeature sf;
	    if (snapshotId == Snapshot.CURRENT_STATE_ID || 
	        (sf = getDirectoryWithSnapshotFeature()) == null) {
	      ReadOnlyList<INode> c = getCurrentChildrenList();
	      final int i = ReadOnlyList.Util.binarySearch(c, name);
	      return i < 0 ? null : c.get(i);
	    }
	    
	    return sf.getChild(this, name, snapshotId);
  }

  /**
   * Search for the given INode in the children list and the deleted lists of
   * snapshots.
   * @return {@link Snapshot#CURRENT_STATE_ID} if the inode is in the children
   * list; {@link Snapshot#NO_SNAPSHOT_ID} if the inode is neither in the
   * children list nor in any snapshot; otherwise the snapshot id of the
   * corresponding snapshot diff list.
 * @throws IOException 
 * @throws ClassNotFoundException 
   */
  public int searchChild(INode inode) {
    INode child = getChild(inode.getLocalNameBytes(), Snapshot.CURRENT_STATE_ID);
    if (child != inode) {
      // inode is not in parent's children list, thus inode must be in
      // snapshot. identify the snapshot id and later add it into the path
      DirectoryDiffList diffs = getDiffs();
      if (diffs == null) {
        return Snapshot.NO_SNAPSHOT_ID;
      }
      return diffs.findSnapshotDeleted(inode);
    } else {
      return Snapshot.CURRENT_STATE_ID;
    }
  }
  
  /**
   * @param snapshotId
   *          if it is not {@link Snapshot#CURRENT_STATE_ID}, get the result
   *          from the corresponding snapshot; otherwise, get the result from
   *          the current directory.
   * @return the current children list if the specified snapshot is null;
   *         otherwise, return the children list corresponding to the snapshot.
   *         Note that the returned list is never null.
   */
  public ReadOnlyList<INode> getChildrenList(final int snapshotId) {
    DirectoryWithSnapshotFeature sf;
    if (snapshotId == Snapshot.CURRENT_STATE_ID
        || (sf = this.getDirectoryWithSnapshotFeature()) == null) {
      return getCurrentChildrenList();
    }
    return sf.getChildrenList(this, snapshotId);
  }
  
  private ReadOnlyList<INode> getCurrentChildrenList() {
    return children == null ? ReadOnlyList.Util.<INode> emptyList()
        : ReadOnlyList.Util.asReadOnlyList(children);
  }

  /**
   * Given a child's name, return the index of the next child
   *
   * @param name a child's name
   * @return the index of the next child
   */
  static int nextChild(ReadOnlyList<INode> children, byte[] name) {
    if (name.length == 0) { // empty name
      return 0;
    }
    int nextPos = ReadOnlyList.Util.binarySearch(children, name) + 1;
    if (nextPos >= 0) {
      return nextPos;
    }
    return -nextPos;
  }
  
  /**
   * Remove the specified child from this directory.
 * @throws IOException 
 * @throws ClassNotFoundException 
   */
  public boolean removeChild(INode child, int latestSnapshotId) {
    if (isInLatestSnapshot(latestSnapshotId)) {
      // create snapshot feature if necessary
      DirectoryWithSnapshotFeature sf = this.getDirectoryWithSnapshotFeature();
      if (sf == null) {
        sf = this.addSnapshotFeature(null);
      }
      return sf.removeChild(this, child, latestSnapshotId);
    }
    return removeChild(child);
  }
  
  public boolean removeChild(INode child, int latestSnapshotId, boolean nvram_enabled, FSDirectory fsd) {
	    if (isInLatestSnapshot(latestSnapshotId)) {
	      // create snapshot feature if necessary
	      DirectoryWithSnapshotFeature sf = this.getDirectoryWithSnapshotFeature();
	      if (sf == null) {
	        sf = this.addSnapshotFeature(null);
	      }
	      return sf.removeChild(this, child, latestSnapshotId);
	    }
	    int location = fsd.NVramMap.get(child.getLocalName());
	    LOG.info("location in removeChild : name = " + child.getLocalName() + 
	    		" location = " + location);
	    boolean result = removeChild(child, nvram_enabled, location);
	    //fsd.NVramMap.remove(child);
	    return result;
	  }
  
  /** 
   * Remove the specified child from this directory.
   * The basic remove method which actually calls children.remove(..).
   *
   * @param child the child inode to be removed
   * 
   * @return true if the child is removed; false if the child is not found.
   */
  public boolean removeChild(final INode child) {
    final int i = searchChildren(child.getLocalNameBytes());
    if (i < 0) {
      return false;
    }

    final INode removed = children.remove(i);
    Preconditions.checkState(removed == child);
    return true;
  }
  
	public boolean removeChild(final INode child, boolean nvram_enabled, int location) {
		int index = 0;
		if (nvram_enabled == true) {
			try {
				if(location  == -1) {
					return false;
				}
				int pos = 0;
				int new_offset = location;
				int root = NativeIO.readIntFromNVRAM(4096, new_offset, pos);
				pos = pos + 4;
				int file_dir = NativeIO.readIntFromNVRAM(4096, new_offset, pos);
				pos = pos + 4;

				INode inode = null;
				if (file_dir == 0) {
					try {
						INodeFile file = FSImageSerialization.readINodeFile(new_offset, pos);
						pos = file.pos;
						inode = (INode) file;
					} catch (IOException e) {
						LOG.info("IOException");
					}
				} else if (file_dir == 1) {
					try {
						INodeDirectory dir = FSImageSerialization.readINodeDir(new_offset, pos);
						pos = dir.pos;
						inode = (INode) dir;
					} catch (IOException e) {
						LOG.info("IOException");
					}
				} else {
					return false;
				}

				int past_pos = pos;
				int commit = NativeIO.readIntFromNVRAM(4096, new_offset, pos);

				pos = pos + 4;
				// LOG.info("buf = " + children_for_nvram);

				if (inode == null)
					return false;

				if (commit == 1) {
					NativeIO.putIntToNVRAM(4096, new_offset, 0, past_pos);
					return true;
				}
		
		
				
//				int inode_num = NativeIO.readIntFromNVRAM(4096, 4096, 0);
//
//				if (inode_num == 0)
//					return false;
//
//				while (index < inode_num) {
//					int pos = 0;
//					// children_for_nvram = NativeIO.allocateNVRAMBuffer(512,
//					// 8192 + (index * 4096));
//					int new_offset = 8192 + (index * 4096);
//					int root = NativeIO.readIntFromNVRAM(4096, new_offset, pos);
//					pos = pos + 4;
//					int file_dir = NativeIO.readIntFromNVRAM(4096, new_offset, pos);
//					pos = pos + 4;
//
//					INode inode = null;
//					if (file_dir == 0) {
//						try {
//							INodeFile file = FSImageSerialization.readINodeFile(new_offset, pos);
//							pos = file.pos;
//							inode = (INode) file;
//						} catch (IOException e) {
//							LOG.info("IOException");
//						}
//					} else if (file_dir == 1) {
//						try {
//							INodeDirectory dir = FSImageSerialization.readINodeDir(new_offset, pos);
//							pos = dir.pos;
//							inode = (INode) dir;
//						} catch (IOException e) {
//							LOG.info("IOException");
//						}
//					} else {
//						return false;
//					}
//
//					int past_pos = pos;
//					int commit = NativeIO.readIntFromNVRAM(4096, new_offset, pos);
//
//					pos = pos + 4;
//					// LOG.info("buf = " + children_for_nvram);
//
//					if (inode == null)
//						return false;
//
//					if (Arrays.equals(child.getLocalNameBytes(), inode.getLocalNameBytes())) {
//						if (commit == 1) {
//
//							NativeIO.putIntToNVRAM(4096, new_offset, 0, past_pos);
//							// NativeIO.putIntToNVRAM(4096, 4096, inode_num -1,
//							// 0);
//							return true;
//						} else {
//
//							index = index + 1;
//						}
//					} else {
//
//						index = index + 1;
//					}
//				}
			} catch (NativeIOException e) {
				LOG.info("native IO Exception occur");
			}
			return false;

		} else {
			final int i = searchChildren(child.getLocalNameBytes());
			if (i < 0) {
				return false;
			}

			final INode removed = children.remove(i);
			Preconditions.checkState(removed == child);
			return true;
		}
	}
	
	public INode[] returnLiveINodeList(int inode_num) {
		int index = 0;
		int list_mem = 0;
		
		INode[] list = new INode[inode_num];
			try {
				while (index < inode_num) {
					int pos = 0;
					int new_offset = 8192 + (index * 4096);
					int root = NativeIO.readIntFromNVRAM(4096, new_offset, pos);
					pos = pos + 4;
					int file_dir = NativeIO.readIntFromNVRAM(4096, new_offset, pos);
					pos = pos + 4;

					INode inode = null;
					if (file_dir == 0) {
						try {
							LOG.info("This is file");
							INodeFile file = FSImageSerialization.readINodeFile(new_offset, pos);
							pos = file.pos;
							file.nvram_location = new_offset;
							inode = (INode) file;
						} catch (IOException e) {
							LOG.info("IOException");
						}
					} else if (file_dir == 1) {
						try {
							LOG.info("This is directory");
							INodeDirectory dir = FSImageSerialization.readINodeDir(new_offset, pos);
							pos = dir.pos;
							dir.nvram_location = new_offset;
							inode = (INode) dir;
						} catch (IOException e) {
							LOG.info("IOException");
						}
					} 

					int past_pos = pos;
					int commit = NativeIO.readIntFromNVRAM(4096, new_offset, pos);
					pos = pos + 4;
					if (commit == 1) {
						LOG.info("return Live = " + inode.getLocalName());
						list[list_mem++] = inode;
						index = index + 1;
						} else {
							LOG.info("return no Live");
							index = index + 1;
						}
				}
			} catch (NativeIOException e) {
				LOG.info("native IO Exception occur");
			}
			return list;
	}

  
	public boolean commitChild(final INode child, BlockCollection bc, boolean nvram_enabled, FSDirectory fsd) {
		if (nvram_enabled == true) {
			int index = 0;
			try {
				int location = fsd.NVramMap.get(child.getLocalName());
				LOG.info("location in commitChild : name = " + child.getLocalName()
				+ " location = " + location);
				if (location == -1)
					return false;
				int pos = 0;
				int new_offset = location;
				int root = NativeIO.readIntFromNVRAM(4096, new_offset, pos);
				pos = pos + 4;
				int file_dir = NativeIO.readIntFromNVRAM(4096, new_offset, pos);
				pos = pos + 4;

				INode inode = null;
				if (file_dir == 0) {
					try {
						INodeFile file = FSImageSerialization.readINodeFile(new_offset, pos);
						pos = file.pos;
						inode = (INode) file;
					} catch (IOException e) {
						LOG.info("IOException");
					}
				} else if (file_dir == 1) {
					try {
						INodeDirectory dir = FSImageSerialization.readINodeDir(new_offset, pos);
						pos = dir.pos;
						inode = (INode) dir;
					} catch (IOException e) {
						LOG.info("IOException");
					}
				} 
				
				int past_pos = pos;
				int commit = NativeIO.readIntFromNVRAM(4096, new_offset, pos);

				pos = pos + 4;

				if (inode == null)
					return false;

			 if (commit == 1) {
						int numblocks = bc.numBlocks();
						NativeIO.putLongToNVRAM(4096, new_offset, bc.getLastBlock().getNumBytes(),
								368 + ((24 * (numblocks - 1)) + 8));
						NativeIO.putIntToNVRAM(4096, new_offset, 2, 152);

						 LOG.info("numblocks in commitChild = " +
						 numblocks + " " +
						 bc.getLastBlock().getNumBytes());
						// bc.getLastBlock().getNumBytes();
						// NativeIO.putIntToNVRAM(4096, 4096, inode_num -1,
						// 0);
						return true;
			 } 
				/**int inode_num = NativeIO.readIntFromNVRAM(4096, 4096, 0);

				if (inode_num == 0)
					return false;

				while (index < inode_num) {
					int pos = 0;
					int new_offset = 8192 + (index * 4096);
					int root = NativeIO.readIntFromNVRAM(4096, new_offset, pos);
					pos = pos + 4;
					int file_dir = NativeIO.readIntFromNVRAM(4096, new_offset, pos);
					pos = pos + 4;

					INode inode = null;
					if (file_dir == 0) {
						try {
							INodeFile file = FSImageSerialization.readINodeFile(new_offset, pos);
							pos = file.pos;
							inode = (INode) file;
						} catch (IOException e) {
							LOG.info("IOException");
						}
					} else if (file_dir == 1) {
						try {
							INodeDirectory dir = FSImageSerialization.readINodeDir(new_offset, pos);
							pos = dir.pos;
							inode = (INode) dir;
						} catch (IOException e) {
							LOG.info("IOException");
						}
					} else {
						return false;
					}
					int past_pos = pos;
					int commit = NativeIO.readIntFromNVRAM(4096, new_offset, pos);

					pos = pos + 4;

					if (inode == null)
						return false;

					if (Arrays.equals(child.getLocalNameBytes(), inode.getLocalNameBytes())) {
						if (commit == 1) {
							int numblocks = bc.numBlocks();
							NativeIO.putLongToNVRAM(4096, new_offset, bc.getLastBlock().getNumBytes(),
									368 + ((24 * (numblocks - 1)) + 8));
							NativeIO.putIntToNVRAM(4096, new_offset, 2, 152);

							// LOG.info("numblocks in commitChild = " +
							// numblocks + " " +
							// bc.getLastBlock().getNumBytes());
							// bc.getLastBlock().getNumBytes();
							// NativeIO.putIntToNVRAM(4096, 4096, inode_num -1,
							// 0);
							return true;
						} else {
							index = index + 1;
						}
					} else {
						index = index + 1;
					}
				}**/
			} catch (NativeIOException e) {
				LOG.info("native IO Exception occur");
			}
			return false;

		} 
		return false;
	}
	
	
  /**
   * Add a child inode to the directory.
   * 
   * @param node INode to insert
   * @param setModTime set modification time for the parent node
   *                   not needed when replaying the addition and 
   *                   the parent already has the proper mod time
   * @return false if the child with this name already exists; 
   *         otherwise, return true;
 * @throws IOException 
   */
  public boolean addChild(INode node, final boolean setModTime,
      final int latestSnapshotId, boolean nvram_enabled, FSDirectory fsd) throws QuotaExceededException {
    final int low = searchChildren(node.getLocalNameBytes(), nvram_enabled);
    if (low >= 0) {
      return false;
    }
    if (isInLatestSnapshot(latestSnapshotId)) {
      // create snapshot feature if  necessary
      DirectoryWithSnapshotFeature sf = this.getDirectoryWithSnapshotFeature();
      if (sf == null) {
        sf = this.addSnapshotFeature(null);
      }
      return sf.addChild(this, node, setModTime, latestSnapshotId, nvram_enabled, fsd);
    }
    addChild(node, low, nvram_enabled, fsd);
    if (setModTime) {
      // update modification time of the parent directory
      updateModificationTime(node.getModificationTime(), latestSnapshotId);
    }
    return true;
  }
  
  public boolean addChild(INode node, final boolean setModTime,
	      final int latestSnapshotId) throws QuotaExceededException {
	    final int low = searchChildren(node.getLocalNameBytes());
	    if (low >= 0) {
	      return false;
	    }
	    if (isInLatestSnapshot(latestSnapshotId)) {
	      // create snapshot feature if  necessary
	      DirectoryWithSnapshotFeature sf = this.getDirectoryWithSnapshotFeature();
	      if (sf == null) {
	        sf = this.addSnapshotFeature(null);
	      }
	      return sf.addChild(this, node, setModTime, latestSnapshotId);
	    }
	    addChild(node, low);
	    if (setModTime) {
	      // update modification time of the parent directory
	      updateModificationTime(node.getModificationTime(), latestSnapshotId);
	    }
	    return true;
	  }

  public boolean addChild(INode node, boolean nvram_enabled, FSDirectory fsd) {
    final int low = searchChildren(node.getLocalNameBytes(), nvram_enabled);

	  if (low >= 0) {
    return false;
   }
    addChild(node, low, nvram_enabled, fsd);
    return true;
  }
  
  public boolean addChild(INode node) {
	    final int low = searchChildren(node.getLocalNameBytes());
	    if (low >= 0) {
	      return false;
	    }
	    addChild(node, low);
	    return true;
	  }

  /**
   * Add the node to the children list at the given insertion point.
   * The basic add method which actually calls children.add(..).
 * @throws IOException 
   */
  /*
   * need to erase code
   */
//  private void addChild(final INode node, final int insertionPoint, boolean nvram_enabled) {
//	    
//	  int last_position = 0;
//	  int inode_num = 0; // except root
//	  int commit = 1;
//	  if(nvram_enabled == true) {
//		  try{
////		  if(children_for_nvram == null) {
////			 try {
////				 LOG.info("nvram allocate_mapping");
////				 children_for_nvram = NativeIO.allocateNVRAMBuffer(4096, 4096);
////				 
////				 LOG.info("Buffer = " + children_for_nvram);
////			} catch (NativeIOException e) {
////				// TODO Auto-generated catch block
////				e.printStackTrace();
////			}			 
////		  }
//////		  children_for_nvram.clear();
////		  LOG.info(" position1 = " + children_for_nvram.position());
//		  //inode_num = children_for_nvram.getInt();
//		  inode_num = NativeIO.readIntFromNVRAM(4096, 4096, 0);
//		  //LOG.info(" inode_num = " + inode_num + " position = " + children_for_nvram.position());
//		  //children_for_nvram.clear();
//		  //LOG.info(" position2 = " + children_for_nvram.position());
//		  inode_num = inode_num + 1;
//		  NativeIO.putIntToNVRAM(4096, 4096, inode_num, 0);
//		  //children_for_nvram.putInt(inode_num + 1);
//		  //LOG.info(" position3 = " + children_for_nvram.position());
//		  //last_position = children_for_nvram.getInt();
//		  //LOG.info(" last_position = " + last_position + " position = " + children_for_nvram.position());
//		  
//			 //NativeIO.freeNVRAMBuffer(children_for_nvram);
//		   //children_for_nvram = NativeIO.allocateNVRAMBuffer(4096, 8192 + 4096 * (inode_num));
//		  //	  LOG.info("buf = " + children_for_nvram );
//		  
//		  int new_offset = 8192 + 4096 * (inode_num - 1);
//		  //byte[] inode_byte = null;
//			node.setParent(this);
//			  if (node.getGroupName() == null) {
//			        node.setGroup(getGroupName());
//			      }
//			int position = 0;
//			if (this.getId() == INodeId.ROOT_INODE_ID) {
//				LOG.info("root");
//				//int current = children_for_nvram.position();
//				//LOG.info("here>? " + children_for_nvram);
//				position = NativeIO.putIntToNVRAM(4096, new_offset, 0, 0);
////				children_for_nvram.putInt(0);
//				LOG.info("nc?");
////				children_for_nvram.position(current);
////				LOG.info("asdf");
////				int rootb = children_for_nvram.getInt();
////				LOG.info("rootb = " + rootb);
//			} else {
//				position = NativeIO.putIntToNVRAM(4096, new_offset, 1, 0);
//				//children_for_nvram.putInt(1);
//			}
//			
//		
//			if(node.isFile()) { // file 0 , directory 1
//				//int currentc = children_for_nvram.position();
//				//children_for_nvram.putInt(0);
//				position = NativeIO.putIntToNVRAM(4096, new_offset, 0, position);
//				LOG.info("addChild after is File() input =" + position);
//				//children_for_nvram.position(currentc);
//				//int fileb = children_for_nvram.getInt();
//			  //LOG.info("file = " + fileb + " " + children_for_nvram);
////			 byte [] node_name = node.getLocalNameBytes();
////			 LOG.info("node_name = " + node_name.toString());
////			 children_for_nvram.putInt(node_name.length);
////			 children_for_nvram.put(node_name);
////			 long fileid = node.getId();
////			 int current = children_for_nvram.position();
////			 children_for_nvram.putLong(fileid);
////			 children_for_nvram.position(current);
////			 long after = children_for_nvram.getLong();
////			 LOG.info(children_for_nvram + " file id = " + fileid +" after " + after );
//			  position = FSImageSerialization.writeINodeFile(node.asFile(), new_offset, ((INodeFile)node).isUnderConstruction(), position);
//			  LOG.info("addChild after writeInodeFile input =" + position);
//			} else if (node.isDirectory()) {
//				position = NativeIO.putIntToNVRAM(4096, new_offset, 0, position);
//			 //LOG.info("dir = " + children_for_nvram );
//			   FSImageSerialization.writeINodeDirectory(node.asDirectory(), new_offset, position);		
//			}
////			LOG.info("test=" + node.getId()+ " " + node.getFullPathName() + " " + node.getLocalNameBytes().toString());
////			byte [] inode_byte = BytesUtil.toByteArray(node);
////			LOG.info("byte = " + inode_byte.toString());
//
//			
////			INode inode = (INode) BytesUtil.toObject(inode_byte);
////			int size = inode_byte.length;
//
////			LOG.info(" before position =" + last_position + "--buf--" + children_for_nvram);
////			children_for_nvram.position(last_position);
////			children_for_nvram.putInt(size);
////			LOG.info(" size = "+ size + " buf =" + children_for_nvram);
////			children_for_nvram.put(inode_byte);
//		//	LOG.info(" position =" + children_for_nvram.position());
//			position = NativeIO.putIntToNVRAM(4096, new_offset, commit, position);
//			LOG.info("addChild after committ =" + position);
//			//children_for_nvram.putInt(commit);
//			//last_position = children_for_nvram[inode_num + 1].position();
//			//LOG.info(" last_position = "+ last_position + " position =" + children_for_nvram[inode_num + 1].position());
//			//children_for_nvram[0].clear();
//			  
//			//LOG.info(" position4 =" + children_for_nvram[0].position());
//			//children_for_nvram[0].position(4);
//			//children_for_nvram[0].putInt(last_position);
//			 
//			//LOG.info(" position5 =" + children_for_nvram[0].position());
//			//children_for_nvram[0].clear();
//			  
//		//	LOG.info(" position6 =" + children_for_nvram.position());
////			NativeIO.freeNVRAMBuffer(children_for_nvram);
//		} catch (IOException e) {
//			// TODO Auto-generated catch block
//			LOG.info("ERROR = " + e.toString() + "Message =" + e.getMessage());
//		}
//		  
//	  }
//	 else {
//	  if (children == null) {
//      children = new ArrayList<INode>(DEFAULT_FILES_PER_DIRECTORY);
//    }
//    node.setParent(this);
//    children.add(-insertionPoint - 1, node);
//
//    if (node.getGroupName() == null) {
//      node.setGroup(getGroupName());
//    }
//	  }
//  }
	private void addChild(final INode node, final int insertionPoint, boolean nvram_enabled, FSDirectory fsd) {

		if (nvram_enabled == true) {
			int last_position = 0;
			int inode_num = 0; // except root
			int commit = 1;

			try {
				inode_num = NativeIO.readIntFromNVRAM(4096, 4096, 0);
				inode_num = inode_num + 1;
				NativeIO.putIntToNVRAM(4096, 4096, inode_num, 0);

				int new_offset = 8192 + 4096 * (inode_num - 1);
				fsd.NVramMap.put(node.getLocalName(), new_offset);
				LOG.info("location in addChild : name =" + node.getLocalName()+
						" offset = " + new_offset);
				
				node.setParent(this);
				if (node.getGroupName() == null) {
					node.setGroup(getGroupName());
				}

				int position = 0;
				if (this.getId() == INodeId.ROOT_INODE_ID) {
					position = NativeIO.putIntToNVRAM(4096, new_offset, 0, 0);
				} else {

					position = NativeIO.putIntToNVRAM(4096, new_offset, 1, 0);
				}

				if (node.isFile()) { // file 0 , directory 1

					position = NativeIO.putIntToNVRAM(4096, new_offset, 0, position);
					position = FSImageSerialization.writeINodeFile(node.asFile(), new_offset,
							((INodeFile) node).isUnderConstruction(), position);
				} else if (node.isDirectory()) {
					position = NativeIO.putIntToNVRAM(4096, new_offset, 1, position);
					position = FSImageSerialization.writeINodeDirectory(node.asDirectory(), new_offset, position);			
				}
				position = NativeIO.putIntToNVRAM(4096, new_offset, commit, position);

			} catch (IOException e) {
				// TODO Auto-generated catch block
				LOG.info("ERROR = " + e.toString() + "Message =" + e.getMessage());
			}

		} else {
			if (children == null) {
				children = new ArrayList<INode>(DEFAULT_FILES_PER_DIRECTORY);
			}
			node.setParent(this);
			children.add(-insertionPoint - 1, node);

			if (node.getGroupName() == null) {
				node.setGroup(getGroupName());
			}
		}
	}
  
  private void addChild(final INode node, final int insertionPoint) {
	
	  if (children == null) {
      children = new ArrayList<INode>(DEFAULT_FILES_PER_DIRECTORY);
    }
    node.setParent(this);
    children.add(-insertionPoint - 1, node);

    if (node.getGroupName() == null) {
      node.setGroup(getGroupName());
    }
  }
  
  

  @Override
  public QuotaCounts computeQuotaUsage(BlockStoragePolicySuite bsps,
      byte blockStoragePolicyId, QuotaCounts counts, boolean useCache,
      int lastSnapshotId) {
    final DirectoryWithSnapshotFeature sf = getDirectoryWithSnapshotFeature();

    // we are computing the quota usage for a specific snapshot here, i.e., the
    // computation only includes files/directories that exist at the time of the
    // given snapshot
    if (sf != null && lastSnapshotId != Snapshot.CURRENT_STATE_ID
        && !(useCache && isQuotaSet())) {
      ReadOnlyList<INode> childrenList = getChildrenList(lastSnapshotId);
      for (INode child : childrenList) {
        final byte childPolicyId = child.getStoragePolicyIDForQuota(blockStoragePolicyId);
        child.computeQuotaUsage(bsps, childPolicyId, counts, useCache,
            lastSnapshotId);
      }
      counts.addNameSpace(1);
      return counts;
    }
    
    // compute the quota usage in the scope of the current directory tree
    final DirectoryWithQuotaFeature q = getDirectoryWithQuotaFeature();
    if (useCache && q != null && q.isQuotaSet()) { // use the cached quota
      return q.AddCurrentSpaceUsage(counts);
    } else {
      useCache = q != null && !q.isQuotaSet() ? false : useCache;
      return computeDirectoryQuotaUsage(bsps, blockStoragePolicyId, counts,
          useCache, lastSnapshotId);
    }
  }

  private QuotaCounts computeDirectoryQuotaUsage(BlockStoragePolicySuite bsps,
      byte blockStoragePolicyId, QuotaCounts counts, boolean useCache,
      int lastSnapshotId) {
    if (children != null) {
      for (INode child : children) {
        final byte childPolicyId = child.getStoragePolicyIDForQuota(blockStoragePolicyId);
        child.computeQuotaUsage(bsps, childPolicyId, counts, useCache,
            lastSnapshotId);
      }
    }
    return computeQuotaUsage4CurrentDirectory(bsps, blockStoragePolicyId,
        counts);
  }
  
  /** Add quota usage for this inode excluding children. */
  public QuotaCounts computeQuotaUsage4CurrentDirectory(
      BlockStoragePolicySuite bsps, byte storagePolicyId, QuotaCounts counts) {
    counts.addNameSpace(1);
    // include the diff list
    DirectoryWithSnapshotFeature sf = getDirectoryWithSnapshotFeature();
    if (sf != null) {
      sf.computeQuotaUsage4CurrentDirectory(bsps, storagePolicyId, counts);
    }
    return counts;
  }

  @Override
  public ContentSummaryComputationContext computeContentSummary(
      ContentSummaryComputationContext summary) {
    final DirectoryWithSnapshotFeature sf = getDirectoryWithSnapshotFeature();
    if (sf != null) {
      sf.computeContentSummary4Snapshot(summary.getBlockStoragePolicySuite(),
          summary.getCounts());
    }
    final DirectoryWithQuotaFeature q = getDirectoryWithQuotaFeature();
    if (q != null) {
      return q.computeContentSummary(this, summary);
    } else {
      return computeDirectoryContentSummary(summary, Snapshot.CURRENT_STATE_ID);
    }
  }

  protected ContentSummaryComputationContext computeDirectoryContentSummary(
      ContentSummaryComputationContext summary, int snapshotId) {
    ReadOnlyList<INode> childrenList = getChildrenList(snapshotId);
    // Explicit traversing is done to enable repositioning after relinquishing
    // and reacquiring locks.
    for (int i = 0;  i < childrenList.size(); i++) {
      INode child = childrenList.get(i);
      byte[] childName = child.getLocalNameBytes();

      long lastYieldCount = summary.getYieldCount();
      child.computeContentSummary(summary);

      // Check whether the computation was paused in the subtree.
      // The counts may be off, but traversing the rest of children
      // should be made safe.
      if (lastYieldCount == summary.getYieldCount()) {
        continue;
      }
      // The locks were released and reacquired. Check parent first.
      if (!isRoot() && getParent() == null) {
        // Stop further counting and return whatever we have so far.
        break;
      }
      // Obtain the children list again since it may have been modified.
      childrenList = getChildrenList(snapshotId);
      // Reposition in case the children list is changed. Decrement by 1
      // since it will be incremented when loops.
      i = nextChild(childrenList, childName) - 1;
    }

    // Increment the directory count for this directory.
    summary.getCounts().addContent(Content.DIRECTORY, 1);
    // Relinquish and reacquire locks if necessary.
    summary.yield();
    return summary;
  }
  
  /**
   * This method is usually called by the undo section of rename.
   * 
   * Before calling this function, in the rename operation, we replace the
   * original src node (of the rename operation) with a reference node (WithName
   * instance) in both the children list and a created list, delete the
   * reference node from the children list, and add it to the corresponding
   * deleted list.
   * 
   * To undo the above operations, we have the following steps in particular:
   * 
   * <pre>
   * 1) remove the WithName node from the deleted list (if it exists) 
   * 2) replace the WithName node in the created list with srcChild 
   * 3) add srcChild back as a child of srcParent. Note that we already add 
   * the node into the created list of a snapshot diff in step 2, we do not need
   * to add srcChild to the created list of the latest snapshot.
   * </pre>
   * 
   * We do not need to update quota usage because the old child is in the 
   * deleted list before. 
   * 
   * @param oldChild
   *          The reference node to be removed/replaced
   * @param newChild
   *          The node to be added back
   * @throws QuotaExceededException should not throw this exception
   */
  public void undoRename4ScrParent(final INodeReference oldChild,
      final INode newChild) throws QuotaExceededException {
    DirectoryWithSnapshotFeature sf = getDirectoryWithSnapshotFeature();
    Preconditions.checkState(sf != null,
        "Directory does not have snapshot feature");
    sf.getDiffs().removeChild(ListType.DELETED, oldChild);
    sf.getDiffs().replaceChild(ListType.CREATED, oldChild, newChild);
    addChild(newChild, true, Snapshot.CURRENT_STATE_ID);
  }
  
  /**
   * Undo the rename operation for the dst tree, i.e., if the rename operation
   * (with OVERWRITE option) removes a file/dir from the dst tree, add it back
   * and delete possible record in the deleted list.  
   */
  public void undoRename4DstParent(final BlockStoragePolicySuite bsps,
      final INode deletedChild,
      int latestSnapshotId) throws QuotaExceededException {
    DirectoryWithSnapshotFeature sf = getDirectoryWithSnapshotFeature();
    Preconditions.checkState(sf != null,
        "Directory does not have snapshot feature");
    boolean removeDeletedChild = sf.getDiffs().removeChild(ListType.DELETED,
        deletedChild);
    int sid = removeDeletedChild ? Snapshot.CURRENT_STATE_ID : latestSnapshotId;
    final boolean added = addChild(deletedChild, true, sid);
    // update quota usage if adding is successfully and the old child has not
    // been stored in deleted list before
    if (added && !removeDeletedChild) {
      final QuotaCounts counts = deletedChild.computeQuotaUsage(bsps);
      addSpaceConsumed(counts, false);

    }
  }

  /** Set the children list to null. */
  public void clearChildren() {
    this.children = null;
  }

  @Override
  public void clear() {
    super.clear();
    clearChildren();
  }

  /** Call cleanSubtree(..) recursively down the subtree. */
  public QuotaCounts cleanSubtreeRecursively(final BlockStoragePolicySuite bsps,
      final int snapshot,
      int prior, final BlocksMapUpdateInfo collectedBlocks,
      final List<INode> removedINodes, final Map<INode, INode> excludedNodes) {
    QuotaCounts counts = new QuotaCounts.Builder().build();
    // in case of deletion snapshot, since this call happens after we modify
    // the diff list, the snapshot to be deleted has been combined or renamed
    // to its latest previous snapshot. (besides, we also need to consider nodes
    // created after prior but before snapshot. this will be done in 
    // DirectoryWithSnapshotFeature)
    int s = snapshot != Snapshot.CURRENT_STATE_ID
        && prior != Snapshot.NO_SNAPSHOT_ID ? prior : snapshot;
    for (INode child : getChildrenList(s)) {
      if (snapshot != Snapshot.CURRENT_STATE_ID && excludedNodes != null
          && excludedNodes.containsKey(child)) {
        continue;
      } else {
        QuotaCounts childCounts = child.cleanSubtree(bsps, snapshot, prior,
            collectedBlocks, removedINodes);
        counts.add(childCounts);
      }
    }
    return counts;
  }

  @Override
  public void destroyAndCollectBlocks(final BlockStoragePolicySuite bsps,
      final BlocksMapUpdateInfo collectedBlocks,
      final List<INode> removedINodes) {
    final DirectoryWithSnapshotFeature sf = getDirectoryWithSnapshotFeature();
    if (sf != null) {
      sf.clear(bsps, this, collectedBlocks, removedINodes);
    }
    for (INode child : getChildrenList(Snapshot.CURRENT_STATE_ID)) {
      child.destroyAndCollectBlocks(bsps, collectedBlocks, removedINodes);
    }
    if (getAclFeature() != null) {
      AclStorage.removeAclFeature(getAclFeature());
    }
    clear();
    removedINodes.add(this);
  }
  
  @Override
  public QuotaCounts cleanSubtree(final BlockStoragePolicySuite bsps,
      final int snapshotId, int priorSnapshotId,
      final BlocksMapUpdateInfo collectedBlocks,
      final List<INode> removedINodes) {
    DirectoryWithSnapshotFeature sf = getDirectoryWithSnapshotFeature();
    // there is snapshot data
    if (sf != null) {
      return sf.cleanDirectory(bsps, this, snapshotId, priorSnapshotId,
          collectedBlocks, removedINodes);
    }
    // there is no snapshot data
    if (priorSnapshotId == Snapshot.NO_SNAPSHOT_ID
        && snapshotId == Snapshot.CURRENT_STATE_ID) {
      // destroy the whole subtree and collect blocks that should be deleted
      QuotaCounts counts = new QuotaCounts.Builder().build();
      this.computeQuotaUsage(bsps, counts, true);
      destroyAndCollectBlocks(bsps, collectedBlocks, removedINodes);
      return counts; 
    } else {
      // process recursively down the subtree
      QuotaCounts counts = cleanSubtreeRecursively(bsps, snapshotId, priorSnapshotId,
          collectedBlocks, removedINodes, null);
      if (isQuotaSet()) {
        getDirectoryWithQuotaFeature().addSpaceConsumed2Cache(counts.negation());
      }
      return counts;
    }
  }
  
  /**
   * Compare the metadata with another INodeDirectory
   */
  @Override
  public boolean metadataEquals(INodeDirectoryAttributes other) {
    return other != null
        && getQuotaCounts().equals(other.getQuotaCounts())
        && getPermissionLong() == other.getPermissionLong()
        && getAclFeature() == other.getAclFeature()
        && getXAttrFeature() == other.getXAttrFeature();
  }
  
  /*
   * The following code is to dump the tree recursively for testing.
   * 
   *      \- foo   (INodeDirectory@33dd2717)
   *        \- sub1   (INodeDirectory@442172)
   *          +- file1   (INodeFile@78392d4)
   *          +- file2   (INodeFile@78392d5)
   *          +- sub11   (INodeDirectory@8400cff)
   *            \- file3   (INodeFile@78392d6)
   *          \- z_file4   (INodeFile@45848712)
   */
  static final String DUMPTREE_EXCEPT_LAST_ITEM = "+-"; 
  static final String DUMPTREE_LAST_ITEM = "\\-";
  @VisibleForTesting
  @Override
  public void dumpTreeRecursively(PrintWriter out, StringBuilder prefix,
      final int snapshot) {
    super.dumpTreeRecursively(out, prefix, snapshot);
    out.print(", childrenSize=" + getChildrenList(snapshot).size());
    final DirectoryWithQuotaFeature q = getDirectoryWithQuotaFeature();
    if (q != null) {
      out.print(", " + q);
    }
    if (this instanceof Snapshot.Root) {
      out.print(", snapshotId=" + snapshot);
    }
    out.println();

    if (prefix.length() >= 2) {
      prefix.setLength(prefix.length() - 2);
      prefix.append("  ");
    }
    dumpTreeRecursively(out, prefix, new Iterable<SnapshotAndINode>() {
      final Iterator<INode> i = getChildrenList(snapshot).iterator();
      
      @Override
      public Iterator<SnapshotAndINode> iterator() {
        return new Iterator<SnapshotAndINode>() {
          @Override
          public boolean hasNext() {
            return i.hasNext();
          }

          @Override
          public SnapshotAndINode next() {
            return new SnapshotAndINode(snapshot, i.next());
          }

          @Override
          public void remove() {
            throw new UnsupportedOperationException();
          }
        };
      }
    });

    final DirectorySnapshottableFeature s = getDirectorySnapshottableFeature();
    if (s != null) {
      s.dumpTreeRecursively(this, out, prefix, snapshot);
    }
  }

  /**
   * Dump the given subtrees.
   * @param prefix The prefix string that each line should print.
   * @param subs The subtrees.
   */
  @VisibleForTesting
  public static void dumpTreeRecursively(PrintWriter out,
      StringBuilder prefix, Iterable<SnapshotAndINode> subs) {
    if (subs != null) {
      for(final Iterator<SnapshotAndINode> i = subs.iterator(); i.hasNext();) {
        final SnapshotAndINode pair = i.next();
        prefix.append(i.hasNext()? DUMPTREE_EXCEPT_LAST_ITEM: DUMPTREE_LAST_ITEM);
        pair.inode.dumpTreeRecursively(out, prefix, pair.snapshotId);
        prefix.setLength(prefix.length() - 2);
      }
    }
  }

  /** A pair of Snapshot and INode objects. */
  public static class SnapshotAndINode implements Serializable {
    public final int snapshotId;
    public final INode inode;

    public SnapshotAndINode(int snapshot, INode inode) {
      this.snapshotId = snapshot;
      this.inode = inode;
    }
  }

  public final int getChildrenNum(final int snapshotId) {
    return getChildrenList(snapshotId).size();
  }
}
