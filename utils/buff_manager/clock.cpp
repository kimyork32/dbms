#include <vector>
#include <iostream>

using namespace std;

int main() {
    cout << "num access, num frames:\n";
    int n, f; 
    if (!(cin >> n >> f)) return 0;
    
    cout << n << " frames input:\n";
    vector<int> a(n);
    for (int i = 0; i < n; i++) {
        cin >> a[i];
    }

    vector<int> frames(f, -1);
    vector<int> use_bits(f, 0);
    int ptr = 0;
    int faults = 0;
    int count = 0;

    for (int i = 0; i < n; i++) {
        cout << "Access " << i + 1 << ": Page " << a[i] << " -> ";
        
        bool hit = false;
        for (int j = 0; j < count; j++) {
            if (frames[j] == a[i]) {
                hit = true;
                use_bits[j] = 1;
                break;
            }
        }

        if (hit) {
            cout << "HIT.          ";
        } else {
            cout << "PAGE FAULT.   ";
            faults++;
            
            if (count < f) {
                frames[count] = a[i];
                use_bits[count] = 1;
                count++;
            } else {
                while (true) {
                    if (use_bits[ptr] == 1) {
                        use_bits[ptr] = 0;
                        ptr = (ptr + 1) % f;
                    } else {
                        frames[ptr] = a[i];
                        use_bits[ptr] = 1;
                        ptr = (ptr + 1) % f;
                        break;
                    }
                }
            }
        }

        cout << "Frames: [";
        for (int j = 0; j < count; j++) {
            cout << frames[j] << "(" << use_bits[j] << ")" << (ptr == j ? "*" : "");
            if (j < count - 1) cout << ", ";
        }
        cout << "]" << endl;
    }

    cout << "pages fails: " << faults << endl;

    return 0;
}