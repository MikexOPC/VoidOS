// ====== suffix_array ======

pub fn suffix_array(symbols: &[u8]) -> Vec<usize> {
    let length = symbols.len();
    let mut order: Vec<usize> = (0..length).collect();
    let mut rank: Vec<usize> = symbols.iter().map(|symbol| usize::from(*symbol)).collect();
    if length < 2 {
        return order;
    }
    let mut next = vec![0usize; length];
    let mut step = 1;
    while step < length {
        let key = |index: usize| (rank[index], rank.get(index + step).map_or(0, |value| value + 1));
        order.sort_unstable_by_key(|index| key(*index));
        next[order[0]] = 0;
        for pair in 1..length {
            let increase = usize::from(key(order[pair]) != key(order[pair - 1]));
            next[order[pair]] = next[order[pair - 1]] + increase;
        }
        std::mem::swap(&mut rank, &mut next);
        if rank[order[length - 1]] == length - 1 {
            break;
        }
        step *= 2;
    }
    order
}

// ====== longest_common_prefixes (Kasai) ======

pub fn longest_common_prefixes(symbols: &[u8], order: &[usize]) -> Vec<usize> {
    let length = symbols.len();
    let mut position = vec![0usize; length];
    for (rank, index) in order.iter().enumerate() {
        position[*index] = rank;
    }
    let mut prefixes = vec![0usize; length.saturating_sub(1)];
    let mut shared = 0;
    for index in 0..length {
        let rank = position[index];
        if rank + 1 == length {
            shared = 0;
            continue;
        }
        let other = order[rank + 1];
        while index + shared < length && other + shared < length && symbols[index + shared] == symbols[other + shared] {
            shared += 1;
        }
        prefixes[rank] = shared;
        shared = shared.saturating_sub(1);
    }
    prefixes
}

// ====== TupleProfile ======

pub struct TupleProfile {
    pub most  : Vec<usize>,
    pub pairs : Vec<f64>,
}

fn root(parents: &mut [usize], mut node: usize) -> usize {
    while parents[node] != node {
        parents[node] = parents[parents[node]];
        node = parents[node];
    }
    node
}

pub fn tuple_profile(symbols: &[u8]) -> TupleProfile {
    let length = symbols.len();
    let prefixes = longest_common_prefixes(symbols, &suffix_array(symbols));
    let longest = prefixes.iter().copied().max().unwrap_or(0);
    let mut buckets = vec![Vec::new(); longest + 1];
    for (index, shared) in prefixes.iter().enumerate() {
        buckets[*shared].push(index);
    }
    let mut parents: Vec<usize> = (0..length).collect();
    let mut sizes = vec![1usize; length];
    let mut most = vec![0usize; longest + 2];
    let mut pairs = vec![0f64; longest + 2];
    let (mut largest, mut total) = (1usize, 0f64);
    for width in (1..=longest).rev() {
        for index in &buckets[width] {
            let (left, right) = (root(&mut parents, *index), root(&mut parents, index + 1));
            let (first, second) = (sizes[left] as f64, sizes[right] as f64);
            total += first * second;
            parents[right] = left;
            sizes[left] += sizes[right];
            largest = largest.max(sizes[left]);
        }
        most[width] = largest;
        pairs[width] = total;
    }
    most[longest + 1] = usize::from(longest < length);
    TupleProfile { most, pairs }
}
