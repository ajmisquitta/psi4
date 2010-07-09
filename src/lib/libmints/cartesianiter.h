#ifndef _psi_src_lib_libmints_cartesianiter_h_
#define _psi_src_lib_libmints_cartesianiter_h_

namespace psi {

/** CartesianIter gives the ordering of the Cartesian functions
    that is used in PSI4. */
class CartesianIter
{
protected:
    int a_;
    int b_;
    int c_;
    int l_;
    int bfn_;

public:
    /// Initialize the iterator for the given angular momentum.
    CartesianIter(int l);
    ~CartesianIter();

    /// Start the iteration.
    virtual void start();
    /// Move to the next Cartesian function.
    virtual void next();
    /// Returns nonzero if the iterator currently holds valid data.
    virtual operator int();

    /// Returns the number of Cartesian functions.
    int n() const { return ((l_>=0)?((((l_)+2)*((l_)+1))>>1):0); }
    /// Returns the x exponent
    int a() const { return a_; }
    /// Returns the y exponent
    int b() const { return b_; }
    /// Returns the z exponent
    int c() const { return c_; }
    /// Return the angular momentum
    int l() const { return l_; }
    /// Returns a() if i==0, b() if i==1, and c() if i==2.
    int l(int i) const { return i ? (i==1 ? b_ : c_) : a_; }
    /** Returns the number of the current basis function within the shell.
        This starts at 0 and sequentially increases as next() is called. */
    int bfn() { return bfn_; }
};

/** RedundantCartesianIter objects loop through all possible combinations
    of a given number of axes.  This is used to compute the transformation
    matrices that maps a set of Cartesian functions to another set of
    Cartesian functions in a rotated coordinate system. */
class RedundantCartesianIter {
private:
    int done_;
    int l_;
    int *axis_;

public:
    /// Create a object for the given angular momentum.
    RedundantCartesianIter(int l);
    virtual ~RedundantCartesianIter();

    /// Return the current Cartesian basis function number.
    virtual int bfn();

    /// Initialize the iterator.
    void start();
    /// Move to the next combination of axes.
    void next();
    /// Returns nonzero if the iterator currently hold valid data.
    operator int() { return !done_; }

    /// The current exponent of x.
    int a() const;
    /// The current exponent of y.
    int b() const;
    /// The current exponent of z.
    int c() const;
    /// The angular momentum.
    int l() const { return l_; }
    /// Returns a() if i==0, b() if i==1, and c() if i==2.
    int l(int i) const;
    /// Return the i'th axis.
    int axis(int i) const { return axis_[i]; }
};

inline void RedundantCartesianIter::start()
{
    if (l_==0)
        done_ = 1;
    else
        done_ = 0;

    for (int i=0; i<l_; i++)
        axis_[i] = 0;
}

inline void RedundantCartesianIter::next()
{
    for (int i=0; i<l_; i++) {
        if (axis_[i] == 2)
            axis_[i] = 0;
        else {
            axis_[i]++;
            return;
        }
    }
    done_ = 1;
}

inline int RedundantCartesianIter::l(int axis) const
{
    int i;
    int r = 0;
    for (i=0; i<l_; i++) if (axis_[i]==axis) r++;
    return r;
}

inline int RedundantCartesianIter::a() const
{
    return l(0);
}

inline int RedundantCartesianIter::b() const
{
    return l(1);
}

inline int RedundantCartesianIter::c() const
{
    return l(2);
}

}

#endif // _psi_src_lib_libmints_cartesianiter_h_

