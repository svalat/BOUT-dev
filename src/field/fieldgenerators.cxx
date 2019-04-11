
#include "fieldgenerators.hxx"

#include <bout/constants.hxx>
#include <utils.hxx>

//////////////////////////////////////////////////////////

FieldGeneratorPtr FieldSin::clone(const std::list<FieldGeneratorPtr> args) {
  if (args.size() != 1) {
    throw ParseException(
        "Incorrect number of arguments to sin function. Expecting 1, got %lu",
        static_cast<unsigned long>(args.size()));
  }

  return std::make_shared<FieldSin>(args.front());
}

BoutReal FieldSin::generate(Position pos) {
  return sin(gen->generate(pos));
}

FieldGeneratorPtr FieldCos::clone(const std::list<FieldGeneratorPtr> args) {
  if (args.size() != 1) {
    throw ParseException(
        "Incorrect number of arguments to cos function. Expecting 1, got %lu",
        static_cast<unsigned long>(args.size()));
  }

  return std::make_shared<FieldCos>(args.front());
}

BoutReal FieldCos::generate(Position pos) {
  return cos(gen->generate(pos));
}

FieldGeneratorPtr FieldSinh::clone(const std::list<FieldGeneratorPtr> args) {
  if (args.size() != 1) {
    throw ParseException(
        "Incorrect number of arguments to sinh function. Expecting 1, got %lu",
        static_cast<unsigned long>(args.size()));
  }

  return std::make_shared<FieldSinh>(args.front());
}

BoutReal FieldSinh::generate(Position pos) {
  return sinh(gen->generate(pos));
}

FieldGeneratorPtr FieldCosh::clone(const std::list<FieldGeneratorPtr> args) {
  if (args.size() != 1) {
    throw ParseException(
        "Incorrect number of arguments to cosh function. Expecting 1, got %lu",
        static_cast<unsigned long>(args.size()));
  }

  return std::make_shared<FieldCosh>(args.front());
}

BoutReal FieldCosh::generate(Position pos) {
  return cosh(gen->generate(pos));
}

FieldGeneratorPtr FieldTanh::clone(const std::list<FieldGeneratorPtr> args) {
  if (args.size() != 1) {
    throw ParseException(
        "Incorrect number of arguments to tanh function. Expecting 1, got %lu",
        static_cast<unsigned long>(args.size()));
  }
  return std::make_shared<FieldTanh>(args.front());
}

BoutReal FieldTanh::generate(Position pos) {
  return tanh(gen->generate(pos));
}

FieldGeneratorPtr FieldGaussian::clone(const std::list<FieldGeneratorPtr> args) {
  if ((args.size() < 1) || (args.size() > 2)) {
    throw ParseException(
        "Incorrect number of arguments to gaussian function. Expecting 1 or 2, got %lu",
        static_cast<unsigned long>(args.size()));
  }

  FieldGeneratorPtr xin = args.front();
  FieldGeneratorPtr sin;
  if(args.size() == 2) {
    sin = args.back(); // Optional second argument
  }else
    sin = std::make_shared<FieldValue>(1.0);

  return std::make_shared<FieldGaussian>(xin, sin);
}

BoutReal FieldGaussian::generate(Position pos) {
  BoutReal sigma = s->generate(pos);
  return exp(-SQ(X->generate(pos)/sigma)/2.) / (sqrt(TWOPI) * sigma);
}

FieldGeneratorPtr FieldAbs::clone(const std::list<FieldGeneratorPtr> args) {
  if (args.size() != 1) {
    throw ParseException(
        "Incorrect number of arguments to abs function. Expecting 1, got %lu",
        static_cast<unsigned long>(args.size()));
  }

  return std::make_shared<FieldAbs>(args.front());
}

BoutReal FieldAbs::generate(Position pos) {
  return std::fabs(gen->generate(pos));
}

FieldGeneratorPtr FieldSqrt::clone(const std::list<FieldGeneratorPtr> args) {
  if (args.size() != 1) {
    throw ParseException(
        "Incorrect number of arguments to sqrt function. Expecting 1, got %lu",
        static_cast<unsigned long>(args.size()));
  }

  return std::make_shared<FieldSqrt>(args.front());
}

BoutReal FieldSqrt::generate(Position pos) {
  return sqrt(gen->generate(pos));
}

FieldGeneratorPtr FieldHeaviside::clone(const std::list<FieldGeneratorPtr> args) {
  if (args.size() != 1) {
    throw ParseException(
        "Incorrect number of arguments to heaviside function. Expecting 1, got %lu",
        static_cast<unsigned long>(args.size()));
  }

  return std::make_shared<FieldHeaviside>(args.front());
}

BoutReal FieldHeaviside::generate(Position pos) {
  return (gen->generate(pos) > 0.0) ? 1.0 : 0.0;
}

FieldGeneratorPtr FieldErf::clone(const std::list<FieldGeneratorPtr> args) {
  if (args.size() != 1) {
    throw ParseException(
        "Incorrect number of arguments to erf function. Expecting 1, got %lu",
        static_cast<unsigned long>(args.size()));
  }

  return std::make_shared<FieldErf>(args.front());
}

BoutReal FieldErf::generate(Position pos) {
  return erf(gen->generate(pos));
}

//////////////////////////////////////////////////////////
// Ballooning transform
// Use a truncated Ballooning transform to enforce periodicity in y and z

FieldGeneratorPtr FieldBallooning::clone(const std::list<FieldGeneratorPtr> args) {
  int n = ball_n;
  switch(args.size()) {
  case 2: {
    // Second optional argument is ball_n, an integer
    // This should probably warn if arg isn't constant
    n = ROUND( args.back()->generate(Position()) );
  } // Fall through
  case 1: {
    return std::make_shared<FieldBallooning>(mesh, args.front(), n);
  }
  };

  throw ParseException("ballooning function must have one or two arguments");
}

BoutReal FieldBallooning::generate(Position pos) {
  if(!mesh)
    throw BoutException("ballooning function needs a valid mesh");
  if(ball_n < 1)
    throw BoutException("ballooning function ball_n less than 1");

  BoutReal ts; // Twist-shift angle
  Coordinates* coords = mesh->getCoordinates();

  int jx = pos.getIx();
  
  if(mesh->periodicY(jx, ts)) {
    // Start with the value at this point
    BoutReal value = arg->generate(pos);

    for(int i=1; i<= ball_n; i++) {
      // y - i * 2pi
      Position cp = pos;
      cp.setY(pos.y() - i*TWOPI);
      cp.setZ(pos.z() + i*ts*TWOPI/coords->zlength());
      value += arg->generate(cp);
      
      cp.setY(pos.y() + i*TWOPI);
      cp.setZ(pos.z() - i*ts*TWOPI/coords->zlength());
      value += arg->generate(cp);
    }
    return value;
  }

  // Open surfaces. Not sure what to do, so set to zero
  return 0.0;
}

////////////////////////////////////////////////////////////////

FieldMixmode::FieldMixmode(FieldGeneratorPtr a, BoutReal seed) : arg(std::move(a)) {
  // Calculate the phases -PI to +PI
  // using genRand [0,1]

  for(int i=0;i<14;i++)
    phase[i] = PI * (2.*genRand(seed + i) - 1.);
}

FieldGeneratorPtr FieldMixmode::clone(const std::list<FieldGeneratorPtr> args) {
  BoutReal seed = 0.5;
  switch(args.size()) {
  case 2: {
    // Second optional argument is the seed, which should be a constant
    seed = args.back()->generate(Position());
  } // Fall through
  case 1: {
    return std::make_shared<FieldMixmode>(args.front(), seed);
  }
  };

  throw ParseException("mixmode function must have one or two arguments");
}

BoutReal FieldMixmode::generate(Position pos) {
  BoutReal result = 0.0;

  // A mixture of mode numbers
  for(int i=0;i<14;i++) {
    // This produces a spectrum which is peaked around mode number 4
    result += ( 1./SQ(1. + std::abs(i - 4)) ) *
      cos(i * arg->generate(pos) + phase[i]);
  }

  return result;
}

BoutReal FieldMixmode::genRand(BoutReal seed) {
  // Make sure seed is
  if(seed < 0.0)
    seed *= -1;

  // Round the seed to get the number of iterations
  int niter = 11 + (23 + ROUND(seed)) % 79;

  // Start x between 0 and 1 (exclusive)
  const BoutReal A = 0.01, B = 1.23456789;
  BoutReal x = (A + fmod(seed,B)) / (B + 2.*A);

  // Iterate logistic map
  for(int i=0;i!=niter;++i)
    x = 3.99 * x * (1. - x);

  return x;
}

//////////////////////////////////////////////////////////
// TanhHat
FieldGeneratorPtr FieldTanhHat::clone(const std::list<FieldGeneratorPtr> args) {
  if (args.size() != 4) {
    throw ParseException(
        "Incorrect number of arguments to TanhHat function. Expecting 4, got %lu",
        static_cast<unsigned long>(args.size()));
  }

  // As lists are not meant to be indexed, we may use an iterator to get the
  // input arguments instead
  // Create the iterator
  auto it = args.begin();
  // Assign the input arguments to the input of the constructor and advance the
  // iterator
  FieldGeneratorPtr xin = *it;
  std::advance(it, 1);
  FieldGeneratorPtr widthin = *it;
  std::advance(it, 1);
  FieldGeneratorPtr centerin = *it;
  std::advance(it, 1);
  FieldGeneratorPtr steepnessin = *it;

  // Call the constructor
  return std::make_shared<FieldTanhHat>(xin, widthin, centerin, steepnessin);
}

BoutReal FieldTanhHat::generate(Position pos) {
  // The following are constants
  BoutReal w = width    ->generate(Position());
  BoutReal c = center   ->generate(Position());
  BoutReal s = steepness->generate(Position());
  return 0.5*(
                 tanh( s*(X->generate(pos) - (c - 0.5*w)) )
               - tanh( s*(X->generate(pos) - (c + 0.5*w)) )
             );
}
