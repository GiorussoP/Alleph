using Godot;
using System.Collections.Generic;
using System.Linq;

public class Octree<T>
{
    private class OctreeNode
    {
        public Aabb Bounds;
        public List<(Vector3 Position, T Data)> Points;
        public OctreeNode[] Children;

        public OctreeNode(Aabb bounds)
        {
            Bounds = bounds;
            Points = new List<(Vector3, T)>();
            Children = new OctreeNode[8];
        }
    }

    private OctreeNode root;
    private int maxDepth;
    private int maxPointsPerNode;

    public Octree(Aabb bounds, int maxDepth = 8, int maxPointsPerNode = 4)
    {
        root = new OctreeNode(bounds);
        this.maxDepth = maxDepth;
        this.maxPointsPerNode = maxPointsPerNode;
    }

    public void Insert(Vector3 position, T data)
    {
        Insert(position, data, root, 0);
    }

    private void Insert(Vector3 position, T data, OctreeNode node, int depth)
    {
        if (depth >= maxDepth)
        {
            node.Points.Add((position, data));
            return;
        }

        if (node.Points.Count < maxPointsPerNode)
        {
            node.Points.Add((position, data));
            return;
        }

        if (node.Children[0] == null)
            Subdivide(node);

        int index = GetChildIndex(node, position);
        Insert(position, data, node.Children[index], depth + 1);
    }

    private void Subdivide(OctreeNode node)
    {
        Vector3 min = node.Bounds.Position;
        Vector3 size = node.Bounds.Size / 2;

        for (int i = 0; i < 8; i++)
        {
            Vector3 offset = new Vector3((i & 1) != 0 ? size.X : 0,
                                         (i & 2) != 0 ? size.Y : 0,
                                         (i & 4) != 0 ? size.Z : 0);

            Aabb childBounds = new Aabb(min + offset, size);
            node.Children[i] = new OctreeNode(childBounds);
        }
    }

    private int GetChildIndex(OctreeNode node, Vector3 position)
    {
        Vector3 min = node.Bounds.Position;
        Vector3 size = node.Bounds.Size / 2;

        int index = 0;
        if (position.X >= min.X + size.X) index |= 1;
        if (position.Y >= min.Y + size.Y) index |= 2;
        if (position.Z >= min.Z + size.Z) index |= 4;

        return index;
    }

    public List<(Vector3 Position,T Data)> KNN(Vector3 queryPosition, int k)
    {
        var knnList = new SortedList<float, (Vector3 Position,T Data)>();
        KNN(queryPosition, root, knnList, k);

        return knnList.Values.ToList();
    }

    private void KNN(Vector3 queryPosition, OctreeNode node, SortedList<float, (Vector3 Position, T Data)> knnList, int k)
    {
        if (node == null) return;

        // Check each point in the node
        foreach (var point in node.Points)
        {
            float distance = queryPosition.DistanceTo(point.Position);
            knnList.Add(distance, point);

            if (knnList.Count > k)
            {
                knnList.RemoveAt(knnList.Count - 1);
            }
        }

        // Recursively check the children nodes
        foreach (var child in node.Children)
        {
            if (child == null) continue;

            // Check if the query position is within the child's bounds
            if (child.Bounds.HasPoint(queryPosition) || knnList.Count < k || child.Bounds.Intersects(new Aabb(queryPosition, new Vector3(knnList.Keys.Last(), knnList.Keys.Last(), knnList.Keys.Last()))))
            {
                KNN(queryPosition, child, knnList, k);
            }
        }
    }

    public bool Remove(Vector3 position, out T data)
    {
        return Remove(position, root, 0, out data);
    }

    private bool Remove(Vector3 position, OctreeNode node, int depth, out T data)
    {
        data = default;

        if (node == null) return false;

        var pointToRemove = node.Points.Find(p => p.Position == position);
        if (!pointToRemove.Equals(default((Vector3, T))))
        {
            node.Points.Remove(pointToRemove);
            data = pointToRemove.Data;
            GD.Print($"Removed point: {position} with data: {data} at depth: {depth}");
            return true;
        }

        if (node.Children[0] == null) return false;

        int index = GetChildIndex(node, position);
        return Remove(position, node.Children[index], depth + 1, out data);
    }

    public bool Find(Vector3 position, out T data) { 
        return Find(position, root, out data);
    }
    private bool Find(Vector3 position, OctreeNode node, out T data) { 
        data = default; 
        if (node == null) 
            return false; 
        var foundPoint = node.Points.Find(p => p.Position == position); 
        if (!foundPoint.Equals(default((Vector3, T)))) { 
            data = foundPoint.Data; 
            GD.Print($"Found point: {position} with data: {data}"); 
            return true; 
        } 
        if (node.Children[0] == null) 
            return false; 
        int index = GetChildIndex(node, position); 
        return Find(position, node.Children[index], out data); 
    }
}
