package org.apache.hadoop.hdfs.server.namenode;

import java.util.ArrayList;

public class DirectoryCacheMember {
	String DiretoryName;
	ArrayList<Integer> Children;
	long id;
	
	public DirectoryCacheMember(String DirectoryName, ArrayList<Integer> Children, long id) {
		this.id = id;
		this.DiretoryName = DirectoryName;
		this.Children = Children;
	}
	
}