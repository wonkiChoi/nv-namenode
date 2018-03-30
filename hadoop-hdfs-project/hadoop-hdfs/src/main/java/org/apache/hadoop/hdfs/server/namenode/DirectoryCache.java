package org.apache.hadoop.hdfs.server.namenode;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.Iterator;
import org.apache.commons.logging.Log;
import org.apache.commons.logging.LogFactory;
import org.apache.commons.logging.impl.Log4JLogger;

import org.apache.hadoop.util.GSet;
import org.apache.hadoop.util.LightWeightGSet;

import com.google.common.base.Preconditions;

public class DirectoryCache {
	
	static final Log LOG = LogFactory.getLog(DirectoryCache.class);
	  static DirectoryCache newInstance(INodeDirectory rootDir) {
	    // Compute the map capacity by allocating 1% of total memory
	    int capacity = LightWeightGSet.computeCapacity(1, "DirectoryCache");
	    ArrayList<DirectoryCacheMember> ReadCache = new ArrayList<>(capacity);
	    return new DirectoryCache(ReadCache);
	  }
	  
	  /** Synchronized by external lock. */
	  //private final GSet<INode, INodeWithAdditionalFields> map;
	  private final ArrayList<DirectoryCacheMember> list;

	  private DirectoryCache(ArrayList<DirectoryCacheMember> list) {
	    Preconditions.checkArgument(list != null);
	    this.list = list;
	  }
	  
	  /**
	   * Add an {@link INode} into the {@link INode} map. Replace the old value if 
	   * necessary. 
	   * @param inode The {@link INode} to be added to the map.
	   */
	  public final void enqueue(DirectoryCacheMember cm) {
			  list.add(cm);
	  }
	  
	  /**
	   * Remove a {@link INode} from the map.
	   * @param inode The {@link INode} to be removed.
	   */
		public final void pop() {
			list.remove(1);
		}
	  
	  /**
	   * @return The size of the map.
	   */
	  public int size() {
	    return list.size();
	  }
	  
	  /**
	   * Get the {@link INode} with the given id from the map.
	   * @param id ID of the {@link INode}.
	   * @return The {@link INode} in the map with the given id. Return null if no 
	   *         such {@link INode} in the map.
	   */
		public ArrayList<Integer> get(long id) {
			Iterator<DirectoryCacheMember> it = list.iterator();
			DirectoryCacheMember cm = null;
			DirectoryCacheMember result = null;
			ArrayList<Integer> result_array = null;
			while (it.hasNext()) {
				cm = it.next();
				if(cm.id == id) {
					result = cm;
					break;
				}
			}
			
			if (result != null)
				result_array = result.Children;
				
			return result_array;		
		}
		
		public boolean isContain(String name) {
			return true;
		}
	  
	  /**
	   * Clear the {@link #map}
	   */
	  public void clear() {
		  list.clear();
	  }

}