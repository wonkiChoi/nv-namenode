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
package org.apache.hadoop.fs.permission;

import org.apache.hadoop.classification.InterfaceAudience;
import org.apache.hadoop.classification.InterfaceStability;
import org.apache.hadoop.io.*;
import org.apache.hadoop.io.nativeio.NativeIO;
import org.apache.hadoop.util.BytesUtil;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.io.DataInput;
import java.io.DataOutput;
import java.io.IOException;
import java.nio.ByteBuffer;

/**
 * Store permission related information.
 */
@InterfaceAudience.LimitedPrivate({"HDFS", "MapReduce"})
@InterfaceStability.Unstable
public class PermissionStatus implements Writable {
	static final Logger LOG = LoggerFactory.getLogger(PermissionStatus.class);
  static final WritableFactory FACTORY = new WritableFactory() {
    @Override
    public Writable newInstance() { return new PermissionStatus(); }
  };
  static {                                      // register a ctor
    WritableFactories.setFactory(PermissionStatus.class, FACTORY);
  }

  /** Create an immutable {@link PermissionStatus} object. */
  public static PermissionStatus createImmutable(
      String user, String group, FsPermission permission) {
    return new PermissionStatus(user, group, permission) {
      @Override
      public PermissionStatus applyUMask(FsPermission umask) {
        throw new UnsupportedOperationException();
      }
      @Override
      public void readFields(DataInput in) throws IOException {
        throw new UnsupportedOperationException();
      }
    };
  }

  private String username;
  private String groupname;
  private FsPermission permission;

  public int pos;
  private PermissionStatus() {}

  /** Constructor */
  public PermissionStatus(String user, String group, FsPermission permission) {
    username = user;
    groupname = group;
    this.permission = permission;
  }

  /** Return user name */
  public String getUserName() {return username;}

  /** Return group name */
  public String getGroupName() {
	  return groupname;
	  }

  /** Return permission */
  public FsPermission getPermission() {return permission;}

  /**
   * Apply umask.
   * @see FsPermission#applyUMask(FsPermission)
   */
  public PermissionStatus applyUMask(FsPermission umask) {
    permission = permission.applyUMask(umask);
    return this;
  }

  @Override
  public void readFields(DataInput in) throws IOException {
    username = Text.readString(in, Text.DEFAULT_MAX_LEN);
    groupname = Text.readString(in, Text.DEFAULT_MAX_LEN);
    permission = FsPermission.read(in);
  }
  
  public void readFields(int new_offset, int pos, long ptr) throws IOException {
	  //int size = NativeIO.readIntFromNVRAM(4096, new_offset, pos);
	  int size = NativeIO.readIntTest(ptr, new_offset + pos);
	  int new_pos = pos + 4;
	  //username = new String(NativeIO.readBAFromNVRAM(4096, new_offset, new_pos, size));
	  username = new String(NativeIO.readBATest(ptr, new_offset + new_pos, size));
	  new_pos = new_pos + 300;
	  //size = NativeIO.readIntFromNVRAM(4096, new_offset, new_pos);
	  size = NativeIO.readIntTest(ptr, new_offset + new_pos);
	  new_pos = new_pos + 4;
	  groupname = new String(NativeIO.readBATest(ptr, new_offset + new_pos, size));
	  new_pos = new_pos + 300;
	  permission = FsPermission.read(new_offset, new_pos, ptr);
	  new_pos = new_pos + 4;
	  this.pos = new_pos;
	  }
  
  public void readFields(ByteBuffer in) throws IOException {
//	    username = Text.readString(in, Text.DEFAULT_MAX_LEN);
//	    groupname = Text.readString(in, Text.DEFAULT_MAX_LEN);
//	    permission = FsPermission.read(in);
	  int size = in.getInt();
	  byte [] user = new byte[size];
	  in.get(user);
	  username = new String(user);
	  size = in.getInt();
	  byte [] group = new byte[size];
	  in.get(group);
	  groupname = new String(group);
	  permission = FsPermission.read(in);
	  }

  @Override
  public void write(DataOutput out) throws IOException {
    write(out, username, groupname, permission);
  }

  /**
   * Create and initialize a {@link PermissionStatus} from {@link DataInput}.
   */
  public static PermissionStatus read(DataInput in) throws IOException {
    PermissionStatus p = new PermissionStatus();
    p.readFields(in);
    return p;
  }
  
  public static PermissionStatus read(ByteBuffer in) throws IOException {
	    PermissionStatus p = new PermissionStatus();
	    p.readFields(in);
	    return p;
	  }
  
  public static PermissionStatus read(int new_offset, int pos, long ptr) throws IOException {
	    PermissionStatus p = new PermissionStatus();
	    p.readFields(new_offset, pos, ptr);
	    return p;
	  }

  /**
   * Serialize a {@link PermissionStatus} from its base components.
   */
  public static void write(DataOutput out,
                           String username, 
                           String groupname,
                           FsPermission permission) throws IOException {
    Text.writeString(out, username, Text.DEFAULT_MAX_LEN);
    Text.writeString(out, groupname, Text.DEFAULT_MAX_LEN);
    permission.write(out);
  }
  
  public static void write(ByteBuffer out,
          String username, 
          String groupname,
          FsPermission permission) throws IOException {
//	  Text.writeString(out, username, Text.DEFAULT_MAX_LEN);
//	  Text.writeString(out, groupname, Text.DEFAULT_MAX_LEN);
	  byte [] byte_usr = username.getBytes();
	  byte [] byte_group = groupname.getBytes();
	  out.putInt(byte_usr.length);
	  out.put(byte_usr);
	  out.putInt(byte_group.length);
	  out.put(byte_group);
	  permission.write(out);
}

  @Override
  public String toString() {
    return username + ":" + groupname + ":" + permission;
  }
}
