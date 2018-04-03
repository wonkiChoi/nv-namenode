package org.apache.hadoop.hdfs.server.namenode;

public class INodeAddress {
	private int location;
	private long parent_id;
	private long id;
	private int child_num;
	private int last_page;
	public INodeAddress () {
		this.location = 0;
		this.parent_id = 0;
		this.id = 0;
		this.child_num = 0;
		this.last_page = 0;
	}
	public INodeAddress (int location, long parent_id , long id, int child_num, int last_page ) {
		this.location = location;
		this.parent_id = parent_id;
		this.id = id;
		this.child_num = child_num;
		this.last_page = last_page;
	}
	
	public int getLocation() {
		return location;
	}
	
	public long getParentId() {
		return parent_id;
	}
	
	public long getId() {
		return id;
	}
	
	public int getNumChild() {
		return child_num;
	}
	
	public int getLastPage() {
		return last_page;
	}
	
	public void setLocation(int location) {
		this.location = location;
	}
	
	public void setParentId(long parent_id) {
		 this.parent_id = parent_id;
	}
	
	public void setId(long id) {
		this.id = id;
	}
	
	public void setNumChild(int child_num) {
		this.child_num = child_num;
	}
	
	public void setLastPage(int last_page) {
		this.last_page = last_page;
	}

}
