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

import java.util.ArrayList;
import java.util.HashMap;
import java.util.Iterator;
import java.util.List;
import org.apache.commons.logging.Log;
import org.apache.commons.logging.LogFactory;
import org.apache.commons.logging.impl.Log4JLogger;

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
	static final Log LOG = LogFactory.getLog(NvramMap.class);
  static NvramMap newInstance(INodeDirectory rootDir) {
    // Compute the map capacity by allocating 1% of total memory
    int capacity = LightWeightGSet.computeCapacity(1, "NvramMap");
    HashMap<String, ArrayList<Integer>> map = new HashMap<>(capacity);
    ArrayList<Integer> l = new ArrayList<Integer>();
    l.add(0);
    map.put(rootDir.getLocalName(), l);
    return new NvramMap(map);
  }
  
  /** Synchronized by external lock. */
  //private final GSet<INode, INodeWithAdditionalFields> map;
  private final HashMap<String, ArrayList<Integer>> map;

  private NvramMap(HashMap<String, ArrayList<Integer>> map) {
    Preconditions.checkArgument(map != null);
    this.map = map;
  }
  
  /**
   * Add an {@link INode} into the {@link INode} map. Replace the old value if 
   * necessary. 
   * @param inode The {@link INode} to be added to the map.
   */
  public final void put(String name, int location) {

	  if(map.containsKey(name)) {
		  map.get(name).add(location); 
		  //LOG.info("second nvram put = "+ name + " location = " + location);
	  } else {
		  ArrayList<Integer> temp = new ArrayList<Integer>();
		  temp.add(location);
		 // LOG.info("first nvram put = "+ name + " location = " + location);
		  map.put(name, temp);
	  }
  }
  
  /**
   * Remove a {@link INode} from the map.
   * @param inode The {@link INode} to be removed.
   */
	public final void remove(String name, int location) {
//		LOG.info("nvram remove = "+ name + " location = " + location);
		for(int i=0; i< map.get(name).size(); i++) {
			if(map.get(name).get(i) == location) {
			//	LOG.info("remove at " + map.get(name).get(i));
				map.get(name).remove(i);
				break;
			}
		}

		if (map.get(name).isEmpty()) {
			//LOG.info("called at " + name);
			map.remove(name);
		}
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
	public ArrayList<Integer> get(String name) {
		if (map.containsKey(name)) {
			return map.get(name);
		} else {
			return null;
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
