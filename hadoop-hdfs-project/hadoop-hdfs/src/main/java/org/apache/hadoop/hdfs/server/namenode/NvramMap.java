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

import java.util.HashMap;
import java.util.Iterator;
import java.util.List;

import org.apache.hadoop.fs.permission.FsPermission;
import org.apache.hadoop.fs.permission.PermissionStatus;
import org.apache.hadoop.hdfs.server.blockmanagement.BlockStoragePolicySuite;
import org.apache.hadoop.util.GSet;
import org.apache.hadoop.util.LightWeightGSet;

import com.google.common.base.Preconditions;

/**
 * Storing all the {@link INode}s and maintaining the mapping between INode ID
 * and INode.  
 */
public class NvramMap {
  
  static NvramMap newInstance(INodeDirectory rootDir) {
    // Compute the map capacity by allocating 1% of total memory
    int capacity = LightWeightGSet.computeCapacity(20, "NvramMap");
    HashMap<String, Integer> map = new HashMap<>(capacity);
    map.put(rootDir.getLocalName(), (int)0);
    return new NvramMap(map);
  }
  
  /** Synchronized by external lock. */
  //private final GSet<INode, INodeWithAdditionalFields> map;
  private final HashMap<String, Integer> map;

  private NvramMap(HashMap<String, Integer> map) {
    Preconditions.checkArgument(map != null);
    this.map = map;
  }
  
  /**
   * Add an {@link INode} into the {@link INode} map. Replace the old value if 
   * necessary. 
   * @param inode The {@link INode} to be added to the map.
   */
  public final void put(String name, int location) {
      map.put(name, location);
  }
  
  /**
   * Remove a {@link INode} from the map.
   * @param inode The {@link INode} to be removed.
   */
  public final void remove(INode inode) {
    map.remove(inode.getLocalName());
  }
  
  /**
   * @return The size of the map.
   */
  public int size() {
    return map.size();
  }
  
  /**
   * Get the {@link INode} with the given id from the map.
   * @param id ID of the {@link INode}.
   * @return The {@link INode} in the map with the given id. Return null if no 
   *         such {@link INode} in the map.
   */
	public int get(String name) {
		if (map.containsKey(name)) {
			return map.get(name);
		} else {
			return -1;
		}
	}
	
	public boolean isContain(String name) {
		if (map.containsKey(name)) {
			return true;
		} else {
			return false;
		}
	}
  
  /**
   * Clear the {@link #map}
   */
  public void clear() {
    map.clear();
  }
}
