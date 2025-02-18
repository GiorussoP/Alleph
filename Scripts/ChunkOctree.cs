using Godot;
using System;
using System.Collections.Generic;



[Tool]
public class ChunkOctree {
    static private readonly float SUBDIVISION_RANGE = 2f, MIN_CHUNK_SIZE = 50;
    public class OctreeNode {
        public int width;
        public Vector3I position;
        public Vector3I center;
        public bool has_children;
        public OctreeNode[] children;

        public MeshInstance3D mesh;
        public CollisionShape3D collision;

        public OctreeNode(Vector3I position, int width){
            this.position = position;
            this.width = width;
            center = position + Vector3I.One * width/2;
            has_children = false;
            children = new OctreeNode[8];
            mesh = new MeshInstance3D();
            collision = new CollisionShape3D();
        }

        public override bool Equals(object obj) {
            if (obj == null || GetType() != obj.GetType())
                return false;

            OctreeNode other = (OctreeNode)obj;
            return center == other.center;
        }
        public override int GetHashCode() {
            return HashCode.Combine(center.X,center.Y,center.Z);
        }
    }
    private OctreeNode root;

    public ChunkOctree(Vector3I position, int width){
        root = new OctreeNode(position, width);
    }

    private float DistanceToChild(OctreeNode child, Vector3 pos){
        return ((Vector3)child.center).DistanceTo(pos);
    }
    private void CreateChildren(OctreeNode node){
        int child_width = node.width/2;


        node.children[0] = new OctreeNode(node.position,                                                            child_width);
        node.children[1] = new OctreeNode(new Vector3I( node.center.X,      node.position.Y,    node.position.Z),   child_width);
        node.children[2] = new OctreeNode(new Vector3I( node.position.X,    node.center.Y,      node.position.Z),   child_width);
        node.children[3] = new OctreeNode(new Vector3I( node.position.X,    node.position.Y,    node.center.Z),     child_width);
        node.children[4] = new OctreeNode(new Vector3I( node.position.X,    node.center.Y,      node.center.Z),     child_width);
        node.children[5] = new OctreeNode(new Vector3I( node.center.X,      node.position.Y,    node.center.Z),     child_width);
        node.children[6] = new OctreeNode(new Vector3I( node.center.X,      node.center.Y,      node.position.Z),   child_width);
        node.children[7] = new OctreeNode(node.center,                                                              child_width);
        
        node.has_children = true;
    }

    public void Insert(Vector3 pos){
        Insert(root,pos);
    }
    private void Insert(OctreeNode node, Vector3 pos){
        float dist = this.DistanceToChild(node, pos);
        if(dist < node.width * SUBDIVISION_RANGE && node.width > MIN_CHUNK_SIZE){
            if(!node.has_children)
                CreateChildren(node);

            foreach(OctreeNode c in node.children){
                Insert(c,pos);
            }
        }
    }

    public HashSet<OctreeNode> getLeafNodes(){
        HashSet<OctreeNode> leaf_nodes = new HashSet<OctreeNode>();
        getLeafNodes(root,leaf_nodes);
        return leaf_nodes;
    }

    private void getLeafNodes(OctreeNode node, HashSet<OctreeNode> leaf_nodes){
        if(!node.has_children){
            leaf_nodes.Add(node);
        }
        else{
            foreach(OctreeNode c in node.children){
                getLeafNodes(c,leaf_nodes);
            }
        }
    }

     public OctreeNode findNeighborChunk(OctreeNode currentChunk, Vector3I direction) {
        Vector3I neighborPosition = currentChunk.position + direction * currentChunk.width;
        return findChunkAtPosition(root, neighborPosition);
    }

    // Recursive method to find a chunk at a specific position
    private OctreeNode findChunkAtPosition(OctreeNode node, Vector3I position) {
        if (node == null) return null;
        
        var bb = new Aabb(node.position, new Vector3I(node.width,node.width,node.width));
        if(bb.HasPoint(position)){
            if(node.has_children){
                OctreeNode n = null;
                foreach(var child in node.children){
                    var ret = findChunkAtPosition(child, position);
                    if(ret != null) n = ret;
                }
                return n;
            }
            else return node;
        }

        return null;
    }
}