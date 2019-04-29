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

import com.google.common.base.Preconditions;
import org.apache.hadoop.fs.FileAlreadyExistsException;
import org.apache.hadoop.fs.InvalidPathException;
import org.apache.hadoop.fs.Options;
import org.apache.hadoop.fs.ParentNotDirectoryException;
import org.apache.hadoop.fs.Path;
import org.apache.hadoop.fs.permission.FsAction;
import org.apache.hadoop.hdfs.DFSUtil;
import org.apache.hadoop.hdfs.DistributedFileSystem;
import org.apache.hadoop.hdfs.protocol.HdfsFileStatus;
import org.apache.hadoop.hdfs.protocol.QuotaExceededException;
import org.apache.hadoop.hdfs.protocol.SnapshotException;
import org.apache.hadoop.hdfs.server.blockmanagement.BlockStoragePolicySuite;
import org.apache.hadoop.hdfs.server.namenode.INode.BlocksMapUpdateInfo;
import org.apache.hadoop.hdfs.server.namenode.snapshot.Snapshot;
import org.apache.hadoop.hdfs.util.ReadOnlyList;
import org.apache.hadoop.io.nativeio.NativeIO;
import org.apache.hadoop.io.nativeio.NativeIOException;
import org.apache.hadoop.util.ChunkedArrayList;
import org.apache.hadoop.util.Time;
import org.apache.commons.logging.Log;
import org.apache.commons.logging.LogFactory;
import org.apache.commons.logging.impl.Log4JLogger;
import java.io.FileNotFoundException;
import java.io.IOException;
import java.util.AbstractMap;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Map;

import static org.apache.hadoop.hdfs.protocol.FSLimitException.MaxDirectoryItemsExceededException;
import static org.apache.hadoop.hdfs.protocol.FSLimitException.PathComponentTooLongException;

class FSDirRenameOp {
	  public static final Log LOG = LogFactory.getLog(FSDirRenameOp.class);
  @Deprecated
  static RenameOldResult renameToInt(
      FSDirectory fsd, final String srcArg, final String dstArg,
      boolean logRetryCache)
      throws IOException {
    String src = srcArg;
    String dst = dstArg;
    if (NameNode.stateChangeLog.isDebugEnabled()) {
      NameNode.stateChangeLog.debug("DIR* NameSystem.renameTo: " + src +
          " to " + dst);
    }
    if (!DFSUtil.isValidName(dst)) {
      throw new IOException("Invalid name: " + dst);
    }
    FSPermissionChecker pc = fsd.getPermissionChecker();

    byte[][] srcComponents = FSDirectory.getPathComponentsForReservedPath(src);
    byte[][] dstComponents = FSDirectory.getPathComponentsForReservedPath(dst);
    HdfsFileStatus resultingStat = null;
    src = fsd.resolvePath(pc, src, srcComponents);
    dst = fsd.resolvePath(pc, dst, dstComponents);
    @SuppressWarnings("deprecation")
    final boolean status = renameTo(fsd, pc, src, dst, logRetryCache);
    if (status) {
      INodesInPath dstIIP = fsd.getINodesInPath(dst, false);
      resultingStat = fsd.getAuditFileInfo(dstIIP);
    }
    return new RenameOldResult(status, resultingStat);
  }

  /**
   * Verify quota for rename operation where srcInodes[srcInodes.length-1] moves
   * dstInodes[dstInodes.length-1]
   */
  private static void verifyQuotaForRename(FSDirectory fsd, INodesInPath src,
      INodesInPath dst) throws QuotaExceededException {
    if (!fsd.getFSNamesystem().isImageLoaded() || fsd.shouldSkipQuotaChecks()) {
      // Do not check quota if edits log is still being processed
      return;
    }
    int i = 0;
    while(src.getINode(i) == dst.getINode(i)) { i++; }
    // src[i - 1] is the last common ancestor.
    BlockStoragePolicySuite bsps = fsd.getBlockStoragePolicySuite();
    final QuotaCounts delta = src.getLastINode().computeQuotaUsage(bsps);

    // Reduce the required quota by dst that is being removed
    final INode dstINode = dst.getLastINode();
    if (dstINode != null) {
      delta.subtract(dstINode.computeQuotaUsage(bsps));
    }
    FSDirectory.verifyQuota(dst, dst.length() - 1, delta, src.getINode(i - 1));
  }

  /**
   * Checks file system limits (max component length and max directory items)
   * during a rename operation.
   */
  static void verifyFsLimitsForRename(FSDirectory fsd, INodesInPath srcIIP,
      INodesInPath dstIIP)
      throws PathComponentTooLongException, MaxDirectoryItemsExceededException {
    byte[] dstChildName = dstIIP.getLastLocalName();
    final String parentPath = dstIIP.getParentPath();
    fsd.verifyMaxComponentLength(dstChildName, parentPath);
    // Do not enforce max directory items if renaming within same directory.
    if (srcIIP.getINode(-2) != dstIIP.getINode(-2)) {
      fsd.verifyMaxDirItems(dstIIP.getINode(-2).asDirectory(), parentPath);
    }
  }

  /**
   * <br>
   * Note: This is to be used by {@link FSEditLogLoader} only.
   * <br>
   */
  @Deprecated
  @SuppressWarnings("deprecation")
  static boolean renameForEditLog(FSDirectory fsd, String src, String dst,
      long timestamp) throws IOException {
    if (fsd.isDir(dst)) {
      dst += Path.SEPARATOR + new Path(src).getName();
    }
    final INodesInPath srcIIP = fsd.getINodesInPath4Write(src, false);
    final INodesInPath dstIIP = fsd.getINodesInPath4Write(dst, false);
    return unprotectedRenameTo(fsd, src, dst, srcIIP, dstIIP, timestamp);
  }

  /**
   * Change a path name
   *
   * @param fsd FSDirectory
   * @param src source path
   * @param dst destination path
   * @return true if rename succeeds; false otherwise
   * @deprecated See {@link #renameToInt(FSDirectory, String, String,
   * boolean, Options.Rename...)}
   */
  @Deprecated
  static boolean unprotectedRenameTo(FSDirectory fsd, String src, String dst,
      final INodesInPath srcIIP, final INodesInPath dstIIP, long timestamp)
      throws IOException {
//	  long startTime = System.currentTimeMillis();
    assert fsd.hasWriteLock();
    final INode srcInode = srcIIP.getLastINode();
    //LOG.info("WONKI rename = " + src + " TO " + dst);
    try {
      validateRenameSource(srcIIP);
    } catch (SnapshotException e) {
      throw e;
    } catch (IOException ignored) {
      return false;
    }

    // validate the destination
    if (dst.equals(src)) {
      return true;
    }

    try {
      validateDestination(src, dst, srcInode);
    } catch (IOException ignored) {
      return false;
    }

    if (dstIIP.getLastINode() != null) {
      NameNode.stateChangeLog.warn("DIR* FSDirectory.unprotectedRenameTo: " +
          "failed to rename " + src + " to " + dst + " because destination " +
          "exists");
      return false;
    }
    INode dstParent = dstIIP.getINode(-2);
    if (dstParent == null) {
      NameNode.stateChangeLog.warn("DIR* FSDirectory.unprotectedRenameTo: " +
          "failed to rename " + src + " to " + dst + " because destination's " +
          "parent does not exist");
      return false;
    }

    fsd.ezManager.checkMoveValidity(srcIIP, dstIIP, src);
    // Ensure dst has quota to accommodate rename
    verifyFsLimitsForRename(fsd, srcIIP, dstIIP);
    verifyQuotaForRename(fsd, srcIIP, dstIIP);

    RenameOperation tx = new RenameOperation(fsd, src, dst, srcIIP, dstIIP);

    boolean added = false;

    try {
			// remove src
			if (!fsd.nvram_enabled) {
				if (!tx.removeSrc4OldRename()) {
					return false;
				}
			}
      //LOG.info("WONKI : here 222");
			if(fsd.advanced_nvram_enabled) {
				added = tx.addSourceToDestinationNVRAM(timestamp);
			} else {
        added = tx.addSourceToDestination(timestamp);
			}
      if (added) {
        if (NameNode.stateChangeLog.isDebugEnabled()) {
          NameNode.stateChangeLog.debug("DIR* FSDirectory" +
              ".unprotectedRenameTo: " + src + " is renamed to " + dst);
        }

        tx.updateMtimeAndLease(timestamp);
        tx.updateQuotasInSourceTree(fsd.getBlockStoragePolicySuite());

        return true;
      }
    } finally {
      if (!added) {
        tx.restoreSource();
      }
    }
    NameNode.stateChangeLog.warn("DIR* FSDirectory.unprotectedRenameTo: " +
        "failed to rename " + src + " to " + dst);
//    long endTime = System.currentTimeMillis();
//    fsd.renameTime = fsd.renameTime + (endTime - startTime);
//    LOG.warn("[checking Time ] renameTime = " + fsd.renameTime);
    return false;
  }

  /**
   * The new rename which has the POSIX semantic.
   */
  static Map.Entry<BlocksMapUpdateInfo, HdfsFileStatus> renameToInt(
      FSDirectory fsd, final String srcArg, final String dstArg,
      boolean logRetryCache, Options.Rename... options)
      throws IOException {
    String src = srcArg;
    String dst = dstArg;
    if (NameNode.stateChangeLog.isDebugEnabled()) {
      NameNode.stateChangeLog.debug("DIR* NameSystem.renameTo: with options -" +
          " " + src + " to " + dst);
    }
    if (!DFSUtil.isValidName(dst)) {
      throw new InvalidPathException("Invalid name: " + dst);
    }
    final FSPermissionChecker pc = fsd.getPermissionChecker();

    byte[][] srcComponents = FSDirectory.getPathComponentsForReservedPath(src);
    byte[][] dstComponents = FSDirectory.getPathComponentsForReservedPath(dst);
    BlocksMapUpdateInfo collectedBlocks = new BlocksMapUpdateInfo();
    src = fsd.resolvePath(pc, src, srcComponents);
    dst = fsd.resolvePath(pc, dst, dstComponents);
    renameTo(fsd, pc, src, dst, collectedBlocks, logRetryCache, options);
    INodesInPath dstIIP = fsd.getINodesInPath(dst, false);
    HdfsFileStatus resultingStat = fsd.getAuditFileInfo(dstIIP);

    return new AbstractMap.SimpleImmutableEntry<>(
        collectedBlocks, resultingStat);
  }

  /**
   * @see {@link #unprotectedRenameTo(FSDirectory, String, String, INodesInPath,
   * INodesInPath, long, BlocksMapUpdateInfo, Options.Rename...)}
   */
  static void renameTo(FSDirectory fsd, FSPermissionChecker pc, String src,
      String dst, BlocksMapUpdateInfo collectedBlocks, boolean logRetryCache,
      Options.Rename... options) throws IOException {
    final INodesInPath srcIIP = fsd.getINodesInPath4Write(src, false);
    final INodesInPath dstIIP = fsd.getINodesInPath4Write(dst, false);
    if (fsd.isPermissionEnabled()) {
      // Rename does not operate on link targets
      // Do not resolveLink when checking permissions of src and dst
      // Check write access to parent of src
      fsd.checkPermission(pc, srcIIP, false, null, FsAction.WRITE, null, null,
          false);
      // Check write access to ancestor of dst
      fsd.checkPermission(pc, dstIIP, false, FsAction.WRITE, null, null, null,
          false);
    }

    if (NameNode.stateChangeLog.isDebugEnabled()) {
      NameNode.stateChangeLog.debug("DIR* FSDirectory.renameTo: " + src + " to "
          + dst);
    }
    final long mtime = Time.now();
    fsd.writeLock();
    try {
			if (fsd.advanced_nvram_enabled) {
				if (unprotectedRenameToNVRAM(fsd, src, dst, srcIIP, dstIIP, mtime, collectedBlocks, options)) {
					FSDirDeleteOp.incrDeletedFileCount(1);
				}
			} else {
				if (unprotectedRenameTo(fsd, src, dst, srcIIP, dstIIP, mtime, collectedBlocks, options)) {
					FSDirDeleteOp.incrDeletedFileCount(1);
				}
			}
    } finally {
      fsd.writeUnlock();
    }
    fsd.getEditLog().logRename(src, dst, mtime, logRetryCache, options);
  }

  /**
   * Rename src to dst.
   * <br>
   * Note: This is to be used by {@link org.apache.hadoop.hdfs.server
   * .namenode.FSEditLogLoader} only.
   * <br>
   *
   * @param fsd       FSDirectory
   * @param src       source path
   * @param dst       destination path
   * @param timestamp modification time
   * @param options   Rename options
   */
  static boolean renameForEditLog(
      FSDirectory fsd, String src, String dst, long timestamp,
      Options.Rename... options)
      throws IOException {
    BlocksMapUpdateInfo collectedBlocks = new BlocksMapUpdateInfo();
    final INodesInPath srcIIP = fsd.getINodesInPath4Write(src, false);
    final INodesInPath dstIIP = fsd.getINodesInPath4Write(dst, false);
    boolean ret = unprotectedRenameTo(fsd, src, dst, srcIIP, dstIIP, timestamp,
        collectedBlocks, options);
    if (!collectedBlocks.getToDeleteList().isEmpty()) {
      fsd.getFSNamesystem().removeBlocksAndUpdateSafemodeTotal(collectedBlocks);
    }
    return ret;
  }

  /**
   * Rename src to dst.
   * See {@link DistributedFileSystem#rename(Path, Path, Options.Rename...)}
   * for details related to rename semantics and exceptions.
   *
   * @param fsd             FSDirectory
   * @param src             source path
   * @param dst             destination path
   * @param timestamp       modification time
   * @param collectedBlocks blocks to be removed
   * @param options         Rename options
   * @return whether a file/directory gets overwritten in the dst path
   */
  static boolean unprotectedRenameTo(FSDirectory fsd, String src, String dst,
      final INodesInPath srcIIP, final INodesInPath dstIIP, long timestamp,
      BlocksMapUpdateInfo collectedBlocks, Options.Rename... options)
      throws IOException {
    assert fsd.hasWriteLock();
    boolean overwrite = options != null
        && Arrays.asList(options).contains(Options.Rename.OVERWRITE);

    final String error;
    final INode srcInode = srcIIP.getLastINode();
    validateRenameSource(srcIIP);

    // validate the destination
    if (dst.equals(src)) {
      throw new FileAlreadyExistsException("The source " + src +
          " and destination " + dst + " are the same");
    }
    validateDestination(src, dst, srcInode);

    if (dstIIP.length() == 1) {
      error = "rename destination cannot be the root";
      NameNode.stateChangeLog.warn("DIR* FSDirectory.unprotectedRenameTo: " +
          error);
      throw new IOException(error);
    }

    BlockStoragePolicySuite bsps = fsd.getBlockStoragePolicySuite();
    fsd.ezManager.checkMoveValidity(srcIIP, dstIIP, src);
    final INode dstInode = dstIIP.getLastINode();
    List<INodeDirectory> snapshottableDirs = new ArrayList<>();
    if (dstInode != null) { // Destination exists
      validateOverwrite(src, dst, overwrite, srcInode, dstInode);
      FSDirSnapshotOp.checkSnapshot(dstInode, snapshottableDirs);
    }

    INode dstParent = dstIIP.getINode(-2);
    if (dstParent == null) {
      error = "rename destination parent " + dst + " not found.";
      NameNode.stateChangeLog.warn("DIR* FSDirectory.unprotectedRenameTo: " +
          error);
      throw new FileNotFoundException(error);
    }
    if (!dstParent.isDirectory()) {
      error = "rename destination parent " + dst + " is a file.";
      NameNode.stateChangeLog.warn("DIR* FSDirectory.unprotectedRenameTo: " +
          error);
      throw new ParentNotDirectoryException(error);
    }

    // Ensure dst has quota to accommodate rename
    verifyFsLimitsForRename(fsd, srcIIP, dstIIP);
    verifyQuotaForRename(fsd, srcIIP, dstIIP);

    RenameOperation tx = new RenameOperation(fsd, src, dst, srcIIP, dstIIP);

    boolean undoRemoveSrc = true;
		if (!fsd.nvram_enabled) {
			tx.removeSrc();
		}
    boolean undoRemoveDst = false;
    long removedNum = 0;
    try {
      if (dstInode != null) { // dst exists, remove it
        removedNum = tx.removeDst();
        if (removedNum != -1) {
          undoRemoveDst = true;
        }
      }
     // LOG.info("WONKI : here 1111");
      // add src as dst to complete rename
      if (tx.addSourceToDestination(timestamp)) {
        undoRemoveSrc = false;
        if (NameNode.stateChangeLog.isDebugEnabled()) {
          NameNode.stateChangeLog.debug("DIR* FSDirectory.unprotectedRenameTo: "
              + src + " is renamed to " + dst);
        }

        tx.updateMtimeAndLease(timestamp);

        // Collect the blocks and remove the lease for previous dst
        boolean filesDeleted = false;
        if (undoRemoveDst) {
          undoRemoveDst = false;
          if (removedNum > 0) {
            filesDeleted = tx.cleanDst(bsps, collectedBlocks);
          }
        }

        if (snapshottableDirs.size() > 0) {
          // There are snapshottable directories (without snapshots) to be
          // deleted. Need to update the SnapshotManager.
          fsd.getFSNamesystem().removeSnapshottableDirs(snapshottableDirs);
        }

        tx.updateQuotasInSourceTree(bsps);
        return filesDeleted;
      }
    } finally {
      if (undoRemoveSrc) {
        tx.restoreSource();
      }
      if (undoRemoveDst) { // Rename failed - restore dst
        tx.restoreDst(bsps);
      }
    }
    NameNode.stateChangeLog.warn("DIR* FSDirectory.unprotectedRenameTo: " +
        "failed to rename " + src + " to " + dst);
    throw new IOException("rename from " + src + " to " + dst + " failed.");
  }
  
  static boolean unprotectedRenameToNVRAM(FSDirectory fsd, String src, String dst,
	      final INodesInPath srcIIP, final INodesInPath dstIIP, long timestamp,
	      BlocksMapUpdateInfo collectedBlocks, Options.Rename... options)
	      throws IOException {
	    assert fsd.hasWriteLock();
	    boolean overwrite = options != null
	        && Arrays.asList(options).contains(Options.Rename.OVERWRITE);

	    final String error;
	    final INode srcInode = srcIIP.getLastINode();
	    validateRenameSource(srcIIP);

	    // validate the destination
	    if (dst.equals(src)) {
	      throw new FileAlreadyExistsException("The source " + src +
	          " and destination " + dst + " are the same");
	    }
	    validateDestination(src, dst, srcInode);

	    if (dstIIP.length() == 1) {
	      error = "rename destination cannot be the root";
	      NameNode.stateChangeLog.warn("DIR* FSDirectory.unprotectedRenameTo: " +
	          error);
	      throw new IOException(error);
	    }

	    BlockStoragePolicySuite bsps = fsd.getBlockStoragePolicySuite();
	    fsd.ezManager.checkMoveValidity(srcIIP, dstIIP, src);
	    final INode dstInode = dstIIP.getLastINode();
	    List<INodeDirectory> snapshottableDirs = new ArrayList<>();
	    if (dstInode != null) { // Destination exists
	      validateOverwrite(src, dst, overwrite, srcInode, dstInode);
	      FSDirSnapshotOp.checkSnapshot(dstInode, snapshottableDirs);
	    }

	    INode dstParent = dstIIP.getINode(-2);
	    if (dstParent == null) {
	      error = "rename destination parent " + dst + " not found.";
	      NameNode.stateChangeLog.warn("DIR* FSDirectory.unprotectedRenameTo: " +
	          error);
	      throw new FileNotFoundException(error);
	    }
	    if (!dstParent.isDirectory()) {
	      error = "rename destination parent " + dst + " is a file.";
	      NameNode.stateChangeLog.warn("DIR* FSDirectory.unprotectedRenameTo: " +
	          error);
	      throw new ParentNotDirectoryException(error);
	    }

	    // Ensure dst has quota to accommodate rename
	    verifyFsLimitsForRename(fsd, srcIIP, dstIIP);
	    verifyQuotaForRename(fsd, srcIIP, dstIIP);

	    RenameOperation tx = new RenameOperation(fsd, src, dst, srcIIP, dstIIP);

	    boolean undoRemoveSrc = true;
			tx.removeSrc();

	    boolean undoRemoveDst = false;
	    long removedNum = 0;
	    try {
	      if (dstInode != null) { // dst exists, remove it
	        removedNum = tx.removeDst();
	        if (removedNum != -1) {
	          undoRemoveDst = true;
	        }
	      }
	     // LOG.info("WONKI : here 1111");
	      // add src as dst to complete rename
	      if (tx.addSourceToDestinationNVRAM(timestamp)) {
	        undoRemoveSrc = false;
	        if (NameNode.stateChangeLog.isDebugEnabled()) {
	          NameNode.stateChangeLog.debug("DIR* FSDirectory.unprotectedRenameTo: "
	              + src + " is renamed to " + dst);
	        }

	        tx.updateMtimeAndLease(timestamp);

	        // Collect the blocks and remove the lease for previous dst
	        boolean filesDeleted = false;
	        if (undoRemoveDst) {
	          undoRemoveDst = false;
	          if (removedNum > 0) {
	            filesDeleted = tx.cleanDst(bsps, collectedBlocks);
	          }
	        }

	        if (snapshottableDirs.size() > 0) {
	          // There are snapshottable directories (without snapshots) to be
	          // deleted. Need to update the SnapshotManager.
	          fsd.getFSNamesystem().removeSnapshottableDirs(snapshottableDirs);
	        }

	        tx.updateQuotasInSourceTree(bsps);
	        return filesDeleted;
	      }
	    } finally {
	      if (undoRemoveSrc) {
	        tx.restoreSource();
	      }
	      if (undoRemoveDst) { // Rename failed - restore dst
	        tx.restoreDst(bsps);
	      }
	    }
	    NameNode.stateChangeLog.warn("DIR* FSDirectory.unprotectedRenameTo: " +
	        "failed to rename " + src + " to " + dst);
	    throw new IOException("rename from " + src + " to " + dst + " failed.");
	  }

  /**
   * @deprecated Use {@link #renameToInt(FSDirectory, String, String,
   * boolean, Options.Rename...)}
   */
  @Deprecated
  @SuppressWarnings("deprecation")
  private static boolean renameTo(FSDirectory fsd, FSPermissionChecker pc,
      String src, String dst, boolean logRetryCache) throws IOException {
    // Rename does not operate on link targets
    // Do not resolveLink when checking permissions of src and dst
    // Check write access to parent of src
    final INodesInPath srcIIP = fsd.getINodesInPath4Write(src, false);
    // Note: We should not be doing this.  This is move() not renameTo().
    final String actualDst = fsd.isDir(dst) ?
        dst + Path.SEPARATOR + new Path(src).getName() : dst;
    final INodesInPath dstIIP = fsd.getINodesInPath4Write(actualDst, false);
    if (fsd.isPermissionEnabled()) {
      fsd.checkPermission(pc, srcIIP, false, null, FsAction.WRITE, null, null,
          false);
      // Check write access to ancestor of dst
      fsd.checkPermission(pc, dstIIP, false, FsAction.WRITE, null, null,
          null, false);
    }

    if (NameNode.stateChangeLog.isDebugEnabled()) {
      NameNode.stateChangeLog.debug("DIR* FSDirectory.renameTo: " + src + " to "
          + dst);
    }
    final long mtime = Time.now();
    boolean stat = false;
    fsd.writeLock();
    try {
      stat = unprotectedRenameTo(fsd, src, actualDst, srcIIP, dstIIP, mtime);
    } finally {
      fsd.writeUnlock();
    }
    if (stat) {
      fsd.getEditLog().logRename(src, actualDst, mtime, logRetryCache);
      return true;
    }
    return false;
  }

  private static void validateDestination(
      String src, String dst, INode srcInode)
      throws IOException {
    String error;
    if (srcInode.isSymlink() &&
        dst.equals(srcInode.asSymlink().getSymlinkString())) {
      throw new FileAlreadyExistsException("Cannot rename symlink " + src
          + " to its target " + dst);
    }
    // dst cannot be a directory or a file under src
    if (dst.startsWith(src)
        && dst.charAt(src.length()) == Path.SEPARATOR_CHAR) {
      error = "Rename destination " + dst
          + " is a directory or file under source " + src;
      NameNode.stateChangeLog.warn("DIR* FSDirectory.unprotectedRenameTo: "
          + error);
      throw new IOException(error);
    }
  }

  private static void validateOverwrite(
      String src, String dst, boolean overwrite, INode srcInode, INode dstInode)
      throws IOException {
    String error;// It's OK to rename a file to a symlink and vice versa
    if (dstInode.isDirectory() != srcInode.isDirectory()) {
      error = "Source " + src + " and destination " + dst
          + " must both be directories";
      NameNode.stateChangeLog.warn("DIR* FSDirectory.unprotectedRenameTo: "
          + error);
      throw new IOException(error);
    }
    if (!overwrite) { // If destination exists, overwrite flag must be true
      error = "rename destination " + dst + " already exists";
      NameNode.stateChangeLog.warn("DIR* FSDirectory.unprotectedRenameTo: "
          + error);
      throw new FileAlreadyExistsException(error);
    }
    if (dstInode.isDirectory()) {
      final ReadOnlyList<INode> children = dstInode.asDirectory()
          .getChildrenList(Snapshot.CURRENT_STATE_ID);
      if (!children.isEmpty()) {
        error = "rename destination directory is not empty: " + dst;
        NameNode.stateChangeLog.warn("DIR* FSDirectory.unprotectedRenameTo: "
            + error);
        throw new IOException(error);
      }
    }
  }

  private static void validateRenameSource(INodesInPath srcIIP)
      throws IOException {
    String error;
    final INode srcInode = srcIIP.getLastINode();
    // validate source
    if (srcInode == null) {
      error = "rename source " + srcIIP.getPath() + " is not found.";
      NameNode.stateChangeLog.warn("DIR* FSDirectory.unprotectedRenameTo: "
          + error);
      throw new FileNotFoundException(error);
    }
    if (srcIIP.length() == 1) {
      error = "rename source cannot be the root";
      NameNode.stateChangeLog.warn("DIR* FSDirectory.unprotectedRenameTo: "
          + error);
      throw new IOException(error);
    }
    // srcInode and its subtree cannot contain snapshottable directories with
    // snapshots
    FSDirSnapshotOp.checkSnapshot(srcInode, null);
  }

  private static class RenameOperation {
    private final FSDirectory fsd;
    private INodesInPath srcIIP;
    private final INodesInPath srcParentIIP;
    private INodesInPath dstIIP;
    private final INodesInPath dstParentIIP;
    private final String src;
    private final String dst;
    private final INodeReference.WithCount withCount;
    private final int srcRefDstSnapshot;
    private final INodeDirectory srcParent;
    private final byte[] srcChildName;
    private final boolean isSrcInSnapshot;
    private final boolean srcChildIsReference;
    private final QuotaCounts oldSrcCounts;
    private INode srcChild;
    private INode oldDstChild;

    RenameOperation(FSDirectory fsd, String src, String dst,
                    INodesInPath srcIIP, INodesInPath dstIIP)
        throws QuotaExceededException {
      this.fsd = fsd;
      this.src = src;
      this.dst = dst;
      this.srcIIP = srcIIP;
      this.dstIIP = dstIIP;
      this.srcParentIIP = srcIIP.getParentINodesInPath();
      this.dstParentIIP = dstIIP.getParentINodesInPath();

      BlockStoragePolicySuite bsps = fsd.getBlockStoragePolicySuite();
      srcChild = this.srcIIP.getLastINode();
      srcChildName = srcChild.getLocalNameBytes();
      final int srcLatestSnapshotId = srcIIP.getLatestSnapshotId();
      isSrcInSnapshot = srcChild.isInLatestSnapshot(srcLatestSnapshotId);
      srcChildIsReference = srcChild.isReference();
      if(fsd.advanced_nvram_enabled) {
    	  srcParent = null;
      } else {
        srcParent = this.srcIIP.getINode(-2).asDirectory();
            }

      // Record the snapshot on srcChild. After the rename, before any new
      // snapshot is taken on the dst tree, changes will be recorded in the
      // latest snapshot of the src tree.
      if (isSrcInSnapshot) {
        srcChild.recordModification(srcLatestSnapshotId);
      }

      // check srcChild for reference
      srcRefDstSnapshot = srcChildIsReference ?
          srcChild.asReference().getDstSnapshotId() : Snapshot.CURRENT_STATE_ID;
      oldSrcCounts = new QuotaCounts.Builder().build();
      if (isSrcInSnapshot) {
        final INodeReference.WithName withName = srcParent
            .replaceChild4ReferenceWithName(srcChild, srcLatestSnapshotId);
        withCount = (INodeReference.WithCount) withName.getReferredINode();
        srcChild = withName;
        this.srcIIP = INodesInPath.replace(srcIIP, srcIIP.length() - 1,
            srcChild);
        // get the counts before rename
        withCount.getReferredINode().computeQuotaUsage(bsps, oldSrcCounts, true);
      } else if (srcChildIsReference) {
        // srcChild is reference but srcChild is not in latest snapshot
        withCount = (INodeReference.WithCount) srcChild.asReference()
            .getReferredINode();
      } else {
        withCount = null;
      }
    }

    long removeSrc() throws IOException {
			long removedNum = 0;
			if (fsd.advanced_nvram_enabled) {
				removedNum = fsd.removeLastINodeNVRAM(srcIIP);
			} else {
				removedNum = fsd.removeLastINode(srcIIP, fsd.nvram_enabled);
			}
      if (removedNum == -1) {
        String error = "Failed to rename " + src + " to " + dst +
            " because the source can not be removed";
        NameNode.stateChangeLog.warn("DIR* FSDirRenameOp.unprotectedRenameTo:" +
            error);
        throw new IOException(error);
      } else {
        // update the quota count if necessary
        fsd.updateCountForDelete(srcChild, srcIIP);
        srcIIP = INodesInPath.replace(srcIIP, srcIIP.length() - 1, null);
        return removedNum;
      }
    }

    boolean removeSrc4OldRename() {
			long removedSrc = 0;
			if (fsd.advanced_nvram_enabled) {
				removedSrc = fsd.removeLastINodeNVRAM(srcIIP);
			} else {
				removedSrc = fsd.removeLastINode(srcIIP, fsd.nvram_enabled);
			}
      if (removedSrc == -1) {
        NameNode.stateChangeLog.warn("DIR* FSDirRenameOp.unprotectedRenameTo: "
            + "failed to rename " + src + " to " + dst + " because the source" +
            " can not be removed");
        return false;
      } else {
        // update the quota count if necessary
        fsd.updateCountForDelete(srcChild, srcIIP);
        srcIIP = INodesInPath.replace(srcIIP, srcIIP.length() - 1, null);
        return true;
      }
    }

    long removeDst() {
			long removedNum = 0;
			if (fsd.advanced_nvram_enabled) {
				removedNum = fsd.removeLastINodeNVRAM(dstIIP);
			} else {
				removedNum = fsd.removeLastINode(dstIIP, fsd.nvram_enabled);
			}
      if (removedNum != -1) {
        oldDstChild = dstIIP.getLastINode();
        // update the quota count if necessary
        fsd.updateCountForDelete(oldDstChild, dstIIP);
        dstIIP = INodesInPath.replace(dstIIP, dstIIP.length() - 1, null);
      }
      return removedNum;
    }

//    boolean addSourceToDestination(long timestamp) {
//      final INode dstParent = dstParentIIP.getLastINode();
//      final byte[] dstChildName = dstIIP.getLastLocalName();
//      String toSrcName = null;
//      INode toSrc = null;
//      String dstParentName = null;
//      String srcParentName = null;
//      long toSrcId = 0;
//      long srcParentId = 0;
//      INode srcParent = null;
//			if (fsd.nvram_enabled) {
//				toSrc = srcIIP.getLastINode();
//				toSrcName = toSrc.getLocalName();
//				toSrcId = toSrc.getId();
//				dstParentName = dstParent.getLocalName();
//				srcParent = srcParentIIP.getLastINode();
//				srcParentName = srcParent.getLocalName();
//				srcParentId = srcParent.getId();
//			}
//
//      final INode toDst;
//      if (withCount == null) {
//        srcChild.setLocalName(dstChildName);
//        toDst = srcChild;
//      } else {
//        withCount.getReferredINode().setLocalName(dstChildName);
//        toDst = new INodeReference.DstReference(dstParent.asDirectory(),
//            withCount, dstIIP.getLatestSnapshotId());
//      }
//			if (fsd.nvram_enabled) {
//				/*Hashmap name modification*/
//				int location = -1;
//				try {
////				LOG.info("WONKI : rename = " + toSrcName + " to " + dstChildName);
//				ArrayList<Integer> temp = fsd.NVramMap.get(toSrcName);
//				
//				if(temp.size() == 1) {
//					location = temp.get(0);
//					fsd.NVramMap.remove(toSrcName, location);
//				} else {
//				for(int i=0; i< temp.size();i++) {
//					if ( NativeIO.readLongTest(FSDirectory.nvramAddress, 316 + temp.get(i)) == toSrcId) {
//						location = temp.get(i);
//						fsd.NVramMap.remove(toSrcName, location);
//					}
//				}
//				}
//				fsd.NVramMap.put(new String(dstChildName), location);
//				//modification parent id
//				NativeIO.putLongTest(FSDirectory.nvramAddress, dstParent.getId(), location);
//				NativeIO.putIntBATest(FSDirectory.nvramAddress, dstChildName.length, dstChildName, location  + 12);
//				NativeIO.putLongTest(FSDirectory.nvramAddress, timestamp, location + 332);
//				
//					// new directory update
//
//					if (dstParent.getId() == INodeId.ROOT_INODE_ID) {
//						if (((INodeDirectory) dstParent).children_location == null) {
//							((INodeDirectory) dstParent).children_location = new ArrayList<Integer>(5);
//						}
//						((INodeDirectory) dstParent).children_location.add(location);
//						((INodeDirectory) dstParent).child_num = ((INodeDirectory) dstParent).child_num + 1;
//					} else {
//
//						int dir_location = -1;
//						ArrayList<Integer> temp2 = fsd.NVramMap.get(dstParentName);
//						if (temp2.size() == 1) {
//							dir_location = temp2.get(0);
//						} else {
//						for (int i = 0; i < temp2.size(); i++) {
//							if (NativeIO.readLongTest(FSDirectory.nvramAddress, 316 + temp2.get(i)) == dstParent
//									.getId()) {
//								dir_location = temp2.get(i);
//							}
//						}
//						}
//						if (dir_location == -1) {
//							LOG.info("WONKI : This error too");
//						}
//
//						int child_num = NativeIO.readIntTest(FSDirectory.nvramAddress, dir_location + 4092 - 4);
//						int next_dir;
//						if (child_num == 782) {
//							next_dir = NativeIO.readIntTest(FSDirectory.nvramAddress, dir_location + 4092 - 8);
//							if (next_dir == 0) {
//								next_dir = allocateNewSpace(fsd);
//								NativeIO.putIntTest(FSDirectory.nvramAddress, next_dir, dir_location + 4092 - 8);
//							}
//							// store next_dir location in current directory
//							int success = addChildrenInDirectoryFast(next_dir, location, fsd);
//
//						} else {
//							int next_location = 4 * (child_num);
//							NativeIO.putIntTest(FSDirectory.nvramAddress, location, dir_location + 952 + next_location);
//							NativeIO.putIntTest(FSDirectory.nvramAddress, child_num + 1, dir_location + 4092 - 4);
//						}
//					}
//
//				//old directory update
//
//					if (srcParent.getId() == INodeId.ROOT_INODE_ID) {
//						((INodeDirectory) srcParent).child_num = ((INodeDirectory) srcParent).child_num - 1;
//						for (int i = 0; i < ((INodeDirectory) srcParent).children_location.size(); i++) {
//							if (((INodeDirectory) srcParent).children_location.get(i) == location) {
//								((INodeDirectory) srcParent).children_location.remove(i);
//								break;
//							}
//						}
//					} else {
//						int dir_location_second = -1;
//						ArrayList<Integer> temp_third = fsd.NVramMap.get(srcParentName);
//						
//						if (temp_third.size() == 1) {
//							dir_location_second = temp_third.get(0);
//						} else {
//						for (int i = 0; i < temp_third.size(); i++) {
//							if (NativeIO.readLongTest(FSDirectory.nvramAddress,
//									316 + temp_third.get(i)) == srcParentId) {
//								dir_location_second = temp_third.get(i);
//							}
//						}
//						}
//						if (dir_location_second == -1) {
//							LOG.info("WONKI : This error too");
//						}
//
//						int child_num = NativeIO.readIntTest(FSDirectory.nvramAddress, dir_location_second + 4092 - 4);
//						int next_dir = NativeIO.readIntTest(FSDirectory.nvramAddress, dir_location_second + 4092 - 8);
//						// if next_dir is 0 exit
//						for (int i = 0; i < child_num; i++) {
//							int record = i * 4;
//							int index = NativeIO.readIntTest(FSDirectory.nvramAddress,
//									dir_location_second + 952 + record);
//							if (index == location) {
//								int offset = NativeIO.readIntTest(FSDirectory.nvramAddress,
//										dir_location_second + 952 + 4 * (child_num - 1));
//								NativeIO.putIntTest(FSDirectory.nvramAddress, offset,
//										dir_location_second + 952 + record);
//								NativeIO.putIntTest(FSDirectory.nvramAddress, 0,
//										dir_location_second + 952 + 4 * (child_num - 1));
//								NativeIO.putIntTest(FSDirectory.nvramAddress, child_num - 1,
//										dir_location_second + 4092 - 4);
//								break;
//							}
//						}
//						int flag = 0;
//
//						while (next_dir != 0) {
//							child_num = NativeIO.readIntTest(FSDirectory.nvramAddress, next_dir + 4092 - 4);
//							for (int i = 0; i < child_num; i++) {
//								int record_location = i * 4;
//								int index = NativeIO.readIntTest(FSDirectory.nvramAddress, next_dir + record_location);
//								if (index == location) {
//									int offset = NativeIO.readIntTest(FSDirectory.nvramAddress,
//											next_dir + 4 * (child_num - 1));
//									NativeIO.putIntTest(FSDirectory.nvramAddress, offset, next_dir + record_location);
//									NativeIO.putIntTest(FSDirectory.nvramAddress, 0, next_dir + 4 * (child_num - 1));
//									NativeIO.putIntTest(FSDirectory.nvramAddress, child_num - 1, next_dir + 4092 - 4);
//									flag = 1;
//									break;
//								}
//							}
//							next_dir = NativeIO.readIntTest(FSDirectory.nvramAddress, next_dir + 4092 - 8);
//							if (flag == 1) {
//								next_dir = 0;
//							}
//						}
//					}
//				} catch (NativeIOException e) {
//					LOG.info("nativeIOException occur");
//				}
//				return true;
//			} else {
//				return fsd.addLastINodeNoQuotaCheck(dstParentIIP, toDst, fsd.nvram_enabled) != null;
//			}
//		}
  
    boolean addSourceToDestination(long timestamp) {
        final INode dstParent = dstParentIIP.getLastINode();
        final byte[] dstChildName = dstIIP.getLastLocalName();
        String toSrcName = null;
        INode toSrc = null;
        String dstParentName = null;
        String srcParentName = null;
        long toSrcId = 0;
        long srcParentId = 0;
        INode srcParent = null;
  			if (fsd.nvram_enabled) {
  				toSrc = srcIIP.getLastINode();
  				toSrcName = toSrc.getLocalName();
  				toSrcId = toSrc.getId();
  				dstParentName = dstParent.getLocalName();
  				srcParent = srcParentIIP.getLastINode();
  				srcParentName = srcParent.getLocalName();
  				srcParentId = srcParent.getId();
  			}

        final INode toDst;
        if (withCount == null) {
          srcChild.setLocalName(dstChildName);
          toDst = srcChild;
        } else {
          withCount.getReferredINode().setLocalName(dstChildName);
          toDst = new INodeReference.DstReference(dstParent.asDirectory(),
              withCount, dstIIP.getLatestSnapshotId());
        }
  			if (fsd.nvram_enabled) {
  				/*Hashmap name modification*/
//  				long reStart = System.currentTimeMillis();
  				int location = -1;
  				try {
//  				LOG.info("WONKI : rename = " + toSrcName + " to " + dstChildName);
  				ArrayList<Integer> temp = fsd.NVramMap.get(toSrcName);
  				
  				if(temp.size() == 1) {
  					location = temp.get(0);
  					fsd.NVramMap.remove(toSrcName, location);
  				} else {
  				for(int i=0; i< temp.size();i++) {
  					if ( NativeIO.readLongTest(FSDirectory.nvramAddress, 316 + temp.get(i)) == toSrcId) {
  						location = temp.get(i);
  						fsd.NVramMap.remove(toSrcName, location);
  					}
  				}
  				}
  				
//  				long resStart = System.currentTimeMillis();
  				fsd.NVramMap.put(new String(dstChildName), location);
  				//modification parent id
  				NativeIO.putLongTest(FSDirectory.nvramAddress, dstParent.getId(), location);
  				NativeIO.putIntBATest(FSDirectory.nvramAddress, dstChildName.length, dstChildName, location  + 12);
  				NativeIO.putLongTest(FSDirectory.nvramAddress, timestamp, location + 332);
  				
  					// new directory update

  					if (dstParent.getId() == INodeId.ROOT_INODE_ID) {
  						if (((INodeDirectory) dstParent).children_location == null) {
  							((INodeDirectory) dstParent).children_location = new ArrayList<Integer>(5);
  						}
  						((INodeDirectory) dstParent).children_location.add(location);
  						((INodeDirectory) dstParent).child_num = ((INodeDirectory) dstParent).child_num + 1;
  					} else {

  						int dir_location = -1;
  						ArrayList<Integer> temp2 = fsd.NVramMap.get(dstParentName);
  						if (temp2.size() == 1) {
  							dir_location = temp2.get(0);
  						} else {
  						for (int i = 0; i < temp2.size(); i++) {
  							if (NativeIO.readLongTest(FSDirectory.nvramAddress, 316 + temp2.get(i)) == dstParent
  									.getId()) {
  								dir_location = temp2.get(i);
  							}
  						}
  						}
  						if (dir_location == -1) {
  							LOG.info("WONKI : This error too");
  						}

  						int success = 0;
  						int children_num = 0;
  						int child_num = NativeIO.readIntTest(FSDirectory.nvramAddress, dir_location + 4092 - 4);
  						int next_dir;
  						int last_page;
						if (child_num == 782) {
							last_page = NativeIO.readIntTest(FSDirectory.nvramAddress, dir_location + 4092 - 12);
							if (last_page == 0) {
								next_dir = allocateNewSpace(fsd);
								NativeIO.putIntTest(FSDirectory.nvramAddress, next_dir, dir_location + 4092 - 8);
								NativeIO.putIntTest(FSDirectory.nvramAddress, next_dir, dir_location + 4092 - 12);
								success = addChildrenInDirectoryFast(next_dir, location, 0);
							} else {
								children_num = NativeIO.readIntTest(FSDirectory.nvramAddress, last_page + 4092 - 4);
								if (children_num == 1021) {
									next_dir = allocateNewSpace(fsd);
									NativeIO.putIntTest(FSDirectory.nvramAddress, next_dir, last_page + 4092 - 8);
									NativeIO.putIntTest(FSDirectory.nvramAddress, next_dir, dir_location + 4092 - 12);
									success = addChildrenInDirectoryFast(next_dir, location, 0);

								} else {
									success = addChildrenInDirectoryFast(last_page, location, children_num);
								}
							}
							// store next_dir location in current directory

						} else {
							int next_location = 4 * (child_num);
							NativeIO.putIntTest(FSDirectory.nvramAddress, location, dir_location + 952 + next_location);
							NativeIO.putIntTest(FSDirectory.nvramAddress, child_num + 1, dir_location + 4092 - 4);
						}
  					}
  					
//  	  		long resExecTime = System.currentTimeMillis() - resStart ;
//  	  		fsd.renameTime_sec = fsd.renameTime_sec + resExecTime;
//  	  		LOG.info("renameChild sec ExecTime = " + fsd.renameTime_sec);

  				//old directory update
//  	  	long retStart = System.currentTimeMillis();
  					if (srcParent.getId() == INodeId.ROOT_INODE_ID) {
  						((INodeDirectory) srcParent).child_num = ((INodeDirectory) srcParent).child_num - 1;
  						for (int i = 0; i < ((INodeDirectory) srcParent).children_location.size(); i++) {
  							if (((INodeDirectory) srcParent).children_location.get(i) == location) {
  								((INodeDirectory) srcParent).children_location.remove(i);
  								break;
  							}
  						}
  					} else {
  						int dir_location_second = -1;
  						ArrayList<Integer> temp_third = fsd.NVramMap.get(srcParentName);
  						
  						if (temp_third.size() == 1) {
  							dir_location_second = temp_third.get(0);
  						} else {
  						for (int i = 0; i < temp_third.size(); i++) {
  							if (NativeIO.readLongTest(FSDirectory.nvramAddress,
  									316 + temp_third.get(i)) == srcParentId) {
  								dir_location_second = temp_third.get(i);
  							}
  						}
  						}
  						if (dir_location_second == -1) {
  							LOG.info("WONKI : This error too");
  						}

  						int child_num = NativeIO.readIntTest(FSDirectory.nvramAddress, dir_location_second + 4092 - 4);
  						int next_dir = NativeIO.readIntTest(FSDirectory.nvramAddress, dir_location_second + 4092 - 8);
  						// if next_dir is 0 exit
  						for (int i = 0; i < child_num; i++) {
  							int record = i * 4;
  							int index = NativeIO.readIntTest(FSDirectory.nvramAddress,
  									dir_location_second + 952 + record);
  							if (index == location) {
  								int offset = NativeIO.readIntTest(FSDirectory.nvramAddress,
  										dir_location_second + 952 + 4 * (child_num - 1));
  								NativeIO.putIntTest(FSDirectory.nvramAddress, offset,
  										dir_location_second + 952 + record);
  								NativeIO.putIntTest(FSDirectory.nvramAddress, 0,
  										dir_location_second + 952 + 4 * (child_num - 1));
  								NativeIO.putIntTest(FSDirectory.nvramAddress, child_num - 1,
  										dir_location_second + 4092 - 4);
  								break;
  							}
  						}
  						int flag = 0;
  						int last_mem = 0;
  						
  						while (next_dir != 0) {
  							last_mem = NativeIO.readIntTest(FSDirectory.nvramAddress, next_dir + 4092 - 12);
  							child_num = NativeIO.readIntTest(FSDirectory.nvramAddress, next_dir + 4092 - 4);
  							
  							if (location > last_mem && last_mem != 0) {
  								next_dir = NativeIO.readIntTest(FSDirectory.nvramAddress, next_dir + 4092 - 8);
  								continue;
  							}
  							for (int i = 0; i < child_num; i++) {
  								int record_location = i * 4;
  								int index = NativeIO.readIntTest(FSDirectory.nvramAddress, next_dir + record_location);
  								if (index == location) {
  									int offset = NativeIO.readIntTest(FSDirectory.nvramAddress,
  											next_dir + 4 * (child_num - 1));
  									NativeIO.putIntTest(FSDirectory.nvramAddress, offset, next_dir + record_location);
  									NativeIO.putIntTest(FSDirectory.nvramAddress, 0, next_dir + 4 * (child_num - 1));
  									NativeIO.putIntTest(FSDirectory.nvramAddress, child_num - 1, next_dir + 4092 - 4);
  									flag = 1;
  									break;
  								}
  							}
  							next_dir = NativeIO.readIntTest(FSDirectory.nvramAddress, next_dir + 4092 - 8);
  							if (flag == 1) {
  								next_dir = 0;
  							}
  						}
  					}
//  		  	  		long retExecTime = System.currentTimeMillis() - retStart ;
//  		  	  		fsd.renameTime_third = fsd.renameTime_third + retExecTime;
//  		  	  		LOG.info("renameChild third add ExecTime = " + retExecTime);
//  		  	  		LOG.info("renameChild third ExecTime = " + fsd.renameTime_third);
  				} catch (NativeIOException e) {
  					LOG.info("nativeIOException occur");
  				}
//  				long reExecTime = System.currentTimeMillis() - reStart ;
//  				fsd.renameTime = fsd.renameTime + reExecTime;
//  				LOG.info("renameChild ExecTime = " + fsd.renameTime);
  				return true;
  			} else {
  				return fsd.addLastINodeNoQuotaCheck(dstParentIIP, toDst, fsd.nvram_enabled) != null;
  			}
  		}
    
    boolean addSourceToDestinationNVRAM(long timestamp) {
        final INode dstParent = dstParentIIP.getLastINode();
        final byte[] dstChildName = dstIIP.getLastLocalName();

        final INode toDst;
        if (withCount == null) {
          srcChild.setLocalName(dstChildName);
          toDst = srcChild;
        } else {
          withCount.getReferredINode().setLocalName(dstChildName);
          toDst = new INodeReference.DstReference(dstParent.asDirectory(),
              withCount, dstIIP.getLatestSnapshotId());
                 }
        
 				return fsd.addLastINodeNoQuotaCheckNVRAM(dstParentIIP, toDst) != null;
   		}

    void updateMtimeAndLease(long timestamp) throws QuotaExceededException {
      //srcParent.updateModificationTime(timestamp, srcIIP.getLatestSnapshotId());
    	srcParentIIP.getLastINode().updateModificationTime(timestamp, srcIIP.getLatestSnapshotId());
      final INode dstParent = dstParentIIP.getLastINode();
      dstParent.updateModificationTime(timestamp, dstIIP.getLatestSnapshotId());
      // update moved lease with new filename
      fsd.getFSNamesystem().unprotectedChangeLease(src, dst);
    }

    void restoreSource() throws QuotaExceededException {
      // Rename failed - restore src
      final INode oldSrcChild = srcChild;
      // put it back
      if (withCount == null) {
        srcChild.setLocalName(srcChildName);
      } else if (!srcChildIsReference) { // src must be in snapshot
        // the withCount node will no longer be used thus no need to update
        // its reference number here
        srcChild = withCount.getReferredINode();
        srcChild.setLocalName(srcChildName);
      } else {
        withCount.removeReference(oldSrcChild.asReference());
        srcChild = new INodeReference.DstReference(srcParent, withCount,
            srcRefDstSnapshot);
        withCount.getReferredINode().setLocalName(srcChildName);
      }

      if (isSrcInSnapshot) {
        srcParent.undoRename4ScrParent(oldSrcChild.asReference(), srcChild);
      } else {
        // srcParent is not an INodeDirectoryWithSnapshot, we only need to add
        // the srcChild back
    	  //LOG.info("wonki yesyeysy");
        fsd.addLastINodeNoQuotaCheck(srcParentIIP, srcChild, fsd.nvram_enabled);
      }
    }

    void restoreDst(BlockStoragePolicySuite bsps) throws QuotaExceededException {
      Preconditions.checkState(oldDstChild != null);
      final INodeDirectory dstParent = dstParentIIP.getLastINode().asDirectory();
      if (dstParent.isWithSnapshot()) {
        dstParent.undoRename4DstParent(bsps, oldDstChild, dstIIP.getLatestSnapshotId());
      } else {
    	  //LOG.info("wonki nononono");
        fsd.addLastINodeNoQuotaCheck(dstParentIIP, oldDstChild, fsd.nvram_enabled);
      }
      if (oldDstChild != null && oldDstChild.isReference()) {
        final INodeReference removedDstRef = oldDstChild.asReference();
        final INodeReference.WithCount wc = (INodeReference.WithCount)
            removedDstRef.getReferredINode().asReference();
        wc.addReference(removedDstRef);
      }
    }

    boolean cleanDst(BlockStoragePolicySuite bsps, BlocksMapUpdateInfo collectedBlocks)
        throws QuotaExceededException {
      Preconditions.checkState(oldDstChild != null);
      List<INode> removedINodes = new ChunkedArrayList<>();
      final boolean filesDeleted;
      if (!oldDstChild.isInLatestSnapshot(dstIIP.getLatestSnapshotId())) {
        oldDstChild.destroyAndCollectBlocks(bsps, collectedBlocks, removedINodes);
        filesDeleted = true;
      } else {
        filesDeleted = oldDstChild.cleanSubtree(bsps, Snapshot.CURRENT_STATE_ID,
            dstIIP.getLatestSnapshotId(), collectedBlocks, removedINodes)
            .getNameSpace() >= 0;
      }
      fsd.getFSNamesystem().removeLeasesAndINodes(src, removedINodes, false);
      return filesDeleted;
    }

    void updateQuotasInSourceTree(BlockStoragePolicySuite bsps) throws QuotaExceededException {
      // update the quota usage in src tree
      if (isSrcInSnapshot) {
        // get the counts after rename
        QuotaCounts newSrcCounts = srcChild.computeQuotaUsage(bsps,
            new QuotaCounts.Builder().build(), false);
        newSrcCounts.subtract(oldSrcCounts);
        srcParent.addSpaceConsumed(newSrcCounts, false);
      }
    }
  }

  static class RenameOldResult {
    final boolean success;
    final HdfsFileStatus auditStat;

    RenameOldResult(boolean success, HdfsFileStatus auditStat) {
      this.success = success;
      this.auditStat = auditStat;
    }
  }
  
	public static int addChildrenInDirectory(int location, int children_offset, FSDirectory fsd) {
		int commit = 1;
		int success = 0;
		try {
			int child_num = NativeIO.readIntTest(FSDirectory.nvramAddress, location + 4092 - 4);
			if (child_num == 1021) {
				int next_dir = NativeIO.readIntTest(FSDirectory.nvramAddress, location + 4092 - 8);
				if (next_dir == 0) {
					next_dir = allocateNewSpace(fsd);
					NativeIO.putIntTest(FSDirectory.nvramAddress, next_dir, location + 4092 - 8);
				}
				return addChildrenInDirectory(next_dir, children_offset, fsd);
			}
			int next_location = 4 * child_num;
			NativeIO.putIntTest(FSDirectory.nvramAddress, children_offset, location + next_location);
			NativeIO.putIntTest(FSDirectory.nvramAddress, child_num + 1, location + 4092 - 4);
		//	NativeIO.putIntTest(FSDirectory.nvramAddress, commit, location + 4092);
			success = 1;
		} catch (IOException e) {
			// TODO Auto-generated catch block
			LOG.info("ERROR = " + e.toString() + "Message =" + e.getMessage());
		}
		return success;
	}
	
	public static int addChildrenInDirectoryFast(int location, int children_offset, int child_num) {
		int success = 0;
		try {
			int next_location = 4 * child_num;
			NativeIO.putIntTest(FSDirectory.nvramAddress, children_offset, location + next_location);
			NativeIO.putIntTest(FSDirectory.nvramAddress, child_num + 1, location + 4092 - 4);
		//	NativeIO.putIntTest(FSDirectory.nvramAddress, commit, location + 4092);
			success = 1;
		} catch (IOException e) {
			// TODO Auto-generated catch block
			LOG.info("ERROR = " + e.toString() + "Message =" + e.getMessage());
		}
		return success;
	}

	public static int allocateNewSpace(FSDirectory fsd) {
		int new_offset = 0;
//		try {
//			int inode_num = NativeIO.readIntTest(FSDirectory.nvramAddress, 0);
//			inode_num = inode_num + 1;
//			NativeIO.putIntTest(FSDirectory.nvramAddress, inode_num, 0);
			fsd.numINode = fsd.numINode + 1;
//			int inode_num = NativeIO.readIntTest(FSDirectory.nvramAddress, 0);
//			inode_num = inode_num + 1;
//
//			NativeIO.putIntTest(FSDirectory.nvramAddress, inode_num, 0);

		//	new_offset = 4096 + 4096 * (inode_num - 1);
			new_offset = (int)(4096 + 4096 * (fsd.numINode - 1));

//			new_offset = 4096 + 4096 * (inode_num - 1);
//		} catch (NativeIOException e) {
//			LOG.info("nativeio exception occur");
//		}
		return new_offset;
	}
}
