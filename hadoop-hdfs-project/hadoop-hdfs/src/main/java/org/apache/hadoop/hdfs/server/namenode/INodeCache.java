package org.apache.hadoop.hdfs.server.namenode;

import java.io.IOException;
import java.util.LinkedHashMap;
import java.util.Map;
import org.apache.commons.logging.Log;
import org.apache.commons.logging.LogFactory;
import org.apache.commons.logging.impl.Log4JLogger;

public class INodeCache<K, V> extends LinkedHashMap<K,V> {
	static final Log LOG = LogFactory.getLog(INodeCache.class);
	/**
	 * 
	 */
	private static final long serialVersionUID = 1L;
	private int lruSize;
	
	public INodeCache(int size) {
		super(size, 0.75f, true);
		this.lruSize = size;
	}

	@Override
	protected boolean removeEldestEntry(Map.Entry<K, V> eldest) {
		boolean isRemove = size() > lruSize;
		if (isRemove) {
			Object obj = this.get(eldest.getKey());
			if (obj instanceof INode) {
					obj = null;
			}
			this.remove(eldest.getKey());
		}
		return size() > lruSize;
	}

}