#include <vector>
#include <algorithm>
#include <iostream>

using namespace std;

int main() {
    cout << "num access, num frames:\n";
    int n, f; cin >> n >> f;
    
    cout << n << " frames input:\n";
    vector<int> a(n);
    for (int i = 0; i < n; i++) {
        cin >> a[i];
    }

    vector<int> mem; // acts as a queue of frames, front is LRU, back is MRU
    int faults = 0;

    for (int i = 0; i < n; i++) {
        cout << "Access " << i + 1 << ": Page " << a[i] << " -> ";
        auto it = find(mem.begin(), mem.end(), a[i]);
        
        if (it != mem.end()) {
            cout << "HIT.          ";
            // move the accessed page to the end
            int page = *it;
            mem.erase(it);
            mem.push_back(page);
        } else {
            cout << "PAGE FAULT.   ";
            // page fault
            faults++;
            if (mem.size() < (size_t)f) {
                mem.push_back(a[i]);
            } else {
                // remove the least recently used page (the one at the front)
                mem.erase(mem.begin());
                mem.push_back(a[i]);
            }
        }

        cout << "Frames: [";
        for (size_t j = 0; j < mem.size(); j++) {
            cout << mem[j] << (j == mem.size() - 1 ? "" : ", ");
        }
        cout << "]" << endl;
    }

    cout << "pages fails: " << faults << endl;

    return 0;
}
