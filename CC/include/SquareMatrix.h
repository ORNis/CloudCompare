//##########################################################################
//#                                                                        #
//#                               CCLIB                                    #
//#                                                                        #
//#  This program is free software; you can redistribute it and/or modify  #
//#  it under the terms of the GNU Library General Public License as       #
//#  published by the Free Software Foundation; version 2 of the License.  #
//#                                                                        #
//#  This program is distributed in the hope that it will be useful,       #
//#  but WITHOUT ANY WARRANTY; without even the implied warranty of        #
//#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         #
//#  GNU General Public License for more details.                          #
//#                                                                        #
//#          COPYRIGHT: EDF R&D / TELECOM ParisTech (ENST-TSI)             #
//#                                                                        #
//##########################################################################

#ifndef SQUARE_MATRIX_HEADER
#define SQUARE_MATRIX_HEADER

//local
#include "CCCoreLib.h"
#include "CCGeom.h"

//system
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <algorithm>
#include <vector>

#define ROTATE(a,i,j,k,l) { Scalar g = a[i][j]; h = a[k][l]; a[i][j] = g-s*(h+g*tau); a[k][l] = h+s*(g-h*tau); }

namespace CCLib
{

	//! Square matrix
	/** Row-major ordered matrix (i.e. elements are accessed with 'values[row][column]')
	**/
	template <typename Scalar> class SquareMatrixTpl
	{
	public:

		//! Default constructor
		/** Warning: invalid matrix.
		**/
		SquareMatrixTpl() { init(0); }

		//! Constructor with a given size
		/** \param size the (square) matrix dimension
		**/
		SquareMatrixTpl(unsigned size) { init(size); }

		//! Constructor from another matrix
		/** \param mat matrix
		**/
		SquareMatrixTpl(const SquareMatrixTpl& mat)
		{
			if (init(mat.m_matrixSize))
				*this = mat;
		}

		//! "From OpenGl" constructor (float version)
		/** The matrix dimension is automatically set to 4.
			It can be forced to 3 (size_3 = true). In this
			case, only the rotation part will be 'imported'.
			\param M16f a table of 16 floats (OpenGL float transformation matrix)
			\param rotationOnly consider only the roation part (3x3 matrix)
		**/
		SquareMatrixTpl(const float M16f[], bool rotationOnly = false)
		{
			unsigned size = (rotationOnly ? 3 : 4);

			if (init(size))
			{
				for (unsigned r=0; r<size; r++)
					for (unsigned c=0; c<size; c++)
						m_values[r][c] = static_cast<Scalar>(M16f[c*4+r]);
			}
		}

		//! "From OpenGl" constructor (double version)
		/** The matrix dimension is automatically set to 4.
			It can be forced to 3 (size_3 = true). In this
			case, only the rotation part will be 'imported'.
			\param M16d a table of 16 floats (OpenGL double transformation matrix)
			\param rotationOnly consider only the roation part (3x3 matrix)
		**/
		SquareMatrixTpl(const double M16d[], bool rotationOnly = false)
		{
			unsigned size = (rotationOnly ? 3 : 4);

			if (init(size))
			{
				for (unsigned r=0; r<size; r++)
					for (unsigned c=0; c<size; c++)
						m_values[r][c] = static_cast<Scalar>(M16d[c*4+r]);
			}
		}

		//! Default destructor
		virtual ~SquareMatrixTpl()
		{
			invalidate();
		}

		//! Returns matrix size
		inline unsigned size() const { return m_matrixSize; }

		//! Returns matrix validity
		/** Matrix is invalid if its size is 0!
		**/
		inline bool isValid() const { return (m_matrixSize != 0); }

		//! Invalidates matrix
		/** Size is reset to 0.
		**/
		void invalidate()
		{
			if (eigenValues)
			{
				delete[] eigenValues;
				eigenValues = 0;
			}

			if (m_values)
			{
				for (unsigned i=0; i<m_matrixSize; i++)
					if (m_values[i])
						delete[] m_values[i];
				delete[] m_values;
				m_values = 0;
			}

			m_matrixSize = matrixSquareSize = 0;
		}

		//! The matrix rows
		/** public for easy/fast access
		**/
		Scalar** m_values;

		//! Returns pointer to matrix row
		inline Scalar* row(unsigned index) { return m_values[index]; }

		//! Sets a particular matrix value
		void inline setValue(unsigned row, unsigned column, Scalar value)
		{
			m_values[row][column] = value;
		}

		//! Returns a particular matrix value
		Scalar inline getValue(unsigned row, unsigned column) const
		{
			return m_values[row][column];
		}

		//! Matrix copy operator
		SquareMatrixTpl& operator = (const SquareMatrixTpl& B)
		{
			if (m_matrixSize != B.size())
			{
				invalidate();
				init(B.size());
			}

			for (unsigned r=0; r<m_matrixSize; r++)
				for (unsigned c=0; c<m_matrixSize; c++)
					m_values[r][c] = B.m_values[r][c];

			if (B.eigenValues)
			{
				enableEigenValues();
				memcpy(eigenValues,B.eigenValues,sizeof(Scalar)*m_matrixSize);
			}

			return *this;
		}

		//! Addition
		SquareMatrixTpl operator + (const SquareMatrixTpl& B) const
		{
			SquareMatrixTpl C = *this;
			C += B;

			return C;
		}

		//! In-place addition
		const SquareMatrixTpl& operator += (const SquareMatrixTpl& B)
		{
			assert(B.size() == m_matrixSize);

			for (unsigned r=0; r<m_matrixSize; r++)
				for (unsigned c=0; c<m_matrixSize; c++)
					m_values[r][c] += B.m_values[r][c];

			return *this;
		}

		//! Substraction
		SquareMatrixTpl operator - (const SquareMatrixTpl& B) const
		{
			SquareMatrixTpl C = *this;
			C -= B;

			return C;
		}

		//! In-place substraction
		const SquareMatrixTpl& operator -= (const SquareMatrixTpl& B)
		{
			assert(B.size() == m_matrixSize);

			for (unsigned r=0; r<m_matrixSize; r++)
				for (unsigned c=0; c<m_matrixSize; c++)
					m_values[r][c] -= B.m_values[r][c];

			return *this;
		}

		//! Multiplication (M = A*B)
		SquareMatrixTpl operator * (const SquareMatrixTpl& B) const
		{
			assert(B.size() == m_matrixSize);

			SquareMatrixTpl C(m_matrixSize);

			for (unsigned r=0; r<m_matrixSize; r++)
			{
				for (unsigned c=0; c<m_matrixSize; c++)
				{
					Scalar sum = 0;
					for (unsigned k=0; k<m_matrixSize; k++)
						sum += m_values[r][k] * B.m_values[k][c];
					C.m_values[r][c] = sum;
				}
			}

			return C;
		}

		//! Multiplication by a vector
		inline CCVector3 operator * (const CCVector3& V) const
		{
			if (m_matrixSize == 3)
			{

				CCVector3 result;
				apply(V.u,result.u);

				return result;
			}
			else
			{
				return V;
			}
		}

		//! In-place multiplication
		inline const SquareMatrixTpl& operator *= (const SquareMatrixTpl& B)
		{
			*this = (*this) * B;

			return *this;
		}

		//! In-place multiplication by a vector
		/** Vec must have the same size as matrix. Returns Vec = M.Vec.
		**/
		//DGM: deprecated, too slow!
		/*inline void apply(Scalar Vec[]) const
		{
			//we apply matrix to Vec and get the result in a (temporary) vector
			Scalar* V = new Scalar[m_matrixSize];
			if (V)
			{
				apply(Vec,V);
				//we copy the result to Vec
				memcpy(Vec,V,sizeof(Scalar)*m_matrixSize);
				//we release temp vector
				delete[] V;
			}
		}
		//*/

		//! Multiplication by a vector
		/** Vec must have the same size as matrix.
			Returns result = M.Vec.
		**/
		void apply(const Scalar Vec[], Scalar result[]) const
		{
			for (unsigned r=0; r<m_matrixSize; r++)
			{
				Scalar sum = 0;
				for (unsigned k=0; k<m_matrixSize; k++)
					sum += m_values[r][k] * static_cast<Scalar>(Vec[k]);
				result[r] = sum;
			}
		}

		//! In-place transpose
		void transpose()
		{
			for (unsigned r=0; r<m_matrixSize-1; r++)
				for (unsigned c=r+1; c<m_matrixSize; c++)
					std::swap(m_values[r][c],m_values[c][r]);
		}

		//! Returns the transposed version of this matrix
		SquareMatrixTpl transposed() const
		{
			SquareMatrixTpl T(*this);
			T.transpose();

			return T;
		}

		//! Sets all elements to 0
		void clear()
		{
			for (unsigned r=0; r<m_matrixSize; ++r)
				memset(m_values[r],0,sizeof(Scalar)*m_matrixSize);

			if (eigenValues)
				memset(eigenValues,0,sizeof(Scalar)*m_matrixSize);
		}

		//! Returns inverse (Gauss)
		SquareMatrixTpl inv() const
		{
			//we create the n by 2n matrix, composed of this matrix and the identity
			Scalar** tempM = 0;
			{
				tempM = new Scalar*[m_matrixSize];
				if (!tempM)
				{
					//not enough memory
					return SquareMatrixTpl();
				}
				for (unsigned i=0; i<m_matrixSize; i++)
				{
					tempM[i] = new Scalar[2*m_matrixSize];
					if (!tempM[i])
					{
						//not enough memory
						for (unsigned j=0; j<i; j++)
							delete[] tempM[j];
						delete[] tempM;
						return SquareMatrixTpl();
					}
				}
			}

			//identity
			{
				for (unsigned i=0; i<m_matrixSize; i++)
				{
					for (unsigned j=0; j<m_matrixSize; j++)
					{
						tempM[i][j] = m_values[i][j];
						if (i == j)
							tempM[i][j+m_matrixSize] = 1;
						else
							tempM[i][j+m_matrixSize] = 0;
					}
				}
			}

			//Gauss pivot
			{
				for (unsigned i=0; i<m_matrixSize; i++)
				{
					//we look for the pivot value (first non zero element)
					unsigned j = i;

					while (tempM[j][i] == 0)
					{
						if (++j >= m_matrixSize)
						{
							//non inversible matrix!
							for (unsigned j=0; j<m_matrixSize; j++)
								delete[] tempM[j];
							delete[] tempM;
							return SquareMatrixTpl();
						}
					}

					//swap the 2 rows if they are different
					//(we only start by the ith element (as all the others are zero!)
					if (i != j)
						for (unsigned k=i; k<2*m_matrixSize; k++)
							std::swap(tempM[i][k],tempM[j][k]);

					//we scale the matrix to make the pivot equal to 1
					if (tempM[i][i] != 1.0)
					{
						const Scalar& tmpVal = tempM[i][i];
						for (unsigned j=i; j<2*m_matrixSize; j++)
							tempM[i][j] /= tmpVal;
					}

					//after the pivot value, all elements are set to zero
					for (unsigned j=i+1; j<m_matrixSize; j++)
					{
						if (tempM[j][i] != 0)
						{
							const Scalar& tmpVal = tempM[j][i];
							for (unsigned k=i; k<2*m_matrixSize; k++)
								tempM[j][k] -= tempM[i][k]*tmpVal;
						}
					}
				}
			}
		
			//reduction
			{
				for (unsigned i = m_matrixSize-1; i>0; i--)
				{
					for (unsigned j=0; j<i; j++)
					{
						if (tempM[j][i] != 0)
						{
							const Scalar& tmpVal = tempM[j][i];
							for (unsigned k=i; k<2*m_matrixSize; k++)
								tempM[j][k] -= tempM[i][k] * tmpVal;
						}
					}
				}
			}

			//result: second part or tempM
			SquareMatrixTpl result(m_matrixSize);
			{
				for (unsigned i=0; i<m_matrixSize; i++)
					for (unsigned j=0; j<m_matrixSize; j++)
						result.m_values[i][j] = tempM[i][j+m_matrixSize];
			}

			//we release temp matrix from memory
			{
				for (unsigned i=0; i<m_matrixSize; i++)
					delete[] tempM[i];
				delete[] tempM;
				tempM = 0;
			}

			return result;
		}

		//! Prints out matrix to console or file
		/** \param fp ASCII FILE handle (or 0 to print to console)
		**/
		void print(FILE* fp = 0) const
		{
			for (unsigned r=0; r<m_matrixSize; r++)
			{
				for (unsigned c=0; c<m_matrixSize; c++)
				{
					if (fp)
						fprintf(fp,"%6.6f ",m_values[r][c]);
					else
						printf("%6.6f ",m_values[r][c]);
				}

				if (fp)
					fprintf(fp,"\n");
				else
					printf("\n");
			}
		}

		//! Sets matrix to identity
		void toIdentity()
		{
			clear();

			for (unsigned r=0; r<m_matrixSize; r++)
				m_values[r][r] = 1;
		}

		//! Scales matrix (all elements are multiplied by the same coef.)
		void scale(Scalar coef)
		{
			for (unsigned r=0; r<m_matrixSize; r++)
				for (unsigned c=0; c<m_matrixSize; c++)
					m_values[r][c] *= coef;
		}

		//! Returns trace
		Scalar trace() const
		{
			Scalar trace = 0;

			for (unsigned r=0; r<m_matrixSize; r++)
				trace += m_values[r][r];

			return trace;
		}

		//! Returns determinant
		double computeDet() const
		{
			return computeSubDet(m_values,m_matrixSize);
		}

		//! Creates a rotation matrix from a quaternion (float version)
		/** Shortcut to double version of initFromQuaternion)
			\param q normalized quaternion (4 float values)
			\return a 3x3 rotation matrix
		**/
		void initFromQuaternion(const float q[])
		{
			double qd[4] = {static_cast<double>(q[0]),
							static_cast<double>(q[1]),
							static_cast<double>(q[2]),
							static_cast<double>(q[3]) };

			initFromQuaternion(qd);
		}

		//! Creates a rotation matrix from a quaternion (double version)
		/** Quaternion is composed of 4 values: an angle (cos(alpha/2))
			and an axis (sin(alpha/2)*unit vector).
			\param q normalized quaternion (w,x,y,z)
			\return a 3x3 rotation matrix
		**/
		void initFromQuaternion(const double q[])
		{
			if (m_matrixSize == 0)
				if (!init(3))
					return;
			assert(m_matrixSize == 3);

			double q00 = q[0]*q[0];
			double q11 = q[1]*q[1];
			double q22 = q[2]*q[2];
			double q33 = q[3]*q[3];
			double q03 = q[0]*q[3];
			double q13 = q[1]*q[3];
			double q23 = q[2]*q[3];
			double q02 = q[0]*q[2];
			double q12 = q[1]*q[2];
			double q01 = q[0]*q[1];

			m_values[0][0] = static_cast<Scalar>(q00 + q11 - q22 - q33);
			m_values[1][1] = static_cast<Scalar>(q00 - q11 + q22 - q33);
			m_values[2][2] = static_cast<Scalar>(q00 - q11 - q22 + q33);
			m_values[0][1] = static_cast<Scalar>(2.0*(q12-q03));
			m_values[1][0] = static_cast<Scalar>(2.0*(q12+q03));
			m_values[0][2] = static_cast<Scalar>(2.0*(q13+q02));
			m_values[2][0] = static_cast<Scalar>(2.0*(q13-q02));
			m_values[1][2] = static_cast<Scalar>(2.0*(q23-q01));
			m_values[2][1] = static_cast<Scalar>(2.0*(q23+q01));
		}

		//! Converts rotation matrix to quaternion
		/** Warning: for 3x3 matrix only!
			From libE57 'best practices' (http://www.libe57.org/best.html)
			\param q quaternion (w,x,y,z)
			\return success
		**/
		bool toQuaternion(double q[/*4*/])
		{
			if (m_matrixSize != 3)
				return false;

			double dTrace =		static_cast<double>(m_values[0][0])
							+	static_cast<double>(m_values[1][1])
							+	static_cast<double>(m_values[2][2])
							+	1.0;
			
			double w,x,y,z;	//quaternion
			if (dTrace > 1.0e-6)
			{
				double S = 2.0 * sqrt(dTrace);
				x = (m_values[2][1] - m_values[1][2]) / S;
				y = (m_values[0][2] - m_values[2][0]) / S;
				z = (m_values[1][0] - m_values[0][1]) / S;
				w = 0.25 * S;	
			}
			else if (m_values[0][0] > m_values[1][1] && m_values[0][0] > m_values[2][2])
			{
				double S = sqrt(1.0 + m_values[0][0] - m_values[1][1] - m_values[2][2]) * 2.0;
				x = 0.25 * S;
				y = (m_values[1][0] + m_values[0][1]) / S;
				z = (m_values[0][2] + m_values[2][0]) / S;
				w = (m_values[2][1] - m_values[1][2]) / S;
			}
			else if (m_values[1][1] > m_values[2][2])
			{
				double S = sqrt(1.0 + m_values[1][1] - m_values[0][0] - m_values[2][2]) * 2.0;
				x = (m_values[1][0] + m_values[0][1]) / S;
				y = 0.25 * S;
				z = (m_values[2][1] + m_values[1][2]) / S;
				w = (m_values[0][2] - m_values[2][0]) / S;
			}
			else
			{
				double S = sqrt(1.0 + m_values[2][2] - m_values[0][0] - m_values[1][1]) * 2.0;
				x = (m_values[0][2] + m_values[2][0]) / S;
				y = (m_values[2][1] + m_values[1][2]) / S;
				z = 0.25 * S;
				w = (m_values[1][0] - m_values[0][1]) / S;
			}

			// normalize the quaternion if the matrix is not a clean rigid body matrix or if it has scaler information.
			double len = sqrt(w*w + x*x + y*y + z*z);
			if (len != 0)
			{	
				q[0] = w/len;
				q[1] = x/len;
				q[2] = y/len;
				q[3] = z/len;

				return true;
			}

			return false;
		}

		//! Returns Delta-determinant (see Kramer formula)
		Scalar deltaDeterminant(unsigned column, Scalar* Vec) const
		{
			SquareMatrixTpl mat(m_matrixSize);

			for (unsigned i=0; i<m_matrixSize; i++)
			{
				if (column == i)
				{
					for (unsigned j=0; j<m_matrixSize; j++)
					{
						mat.m_values[j][i] = static_cast<Scalar>(*Vec);
						Vec++;
					}
				}
				else
				{
					for (unsigned j=0; j<m_matrixSize; j++)
						mat.m_values[j][i] = m_values[j][i];
				}
			}

			return mat.computeDet();
		}

		//! Computes eigen vectors (and values) with the Jacobian method
		/** See numerical recipes.
		**/
		SquareMatrixTpl computeJacobianEigenValuesAndVectors(unsigned maxIterationCount = 50) const
		{
			if (!isValid())
				return SquareMatrixTpl();

			SquareMatrixTpl eigenVectors(m_matrixSize);
			eigenVectors.toIdentity();
			if (!eigenVectors.enableEigenValues())
			{
				//not enough memory
				return SquareMatrixTpl();
			}
			Scalar* d = eigenVectors.eigenValues;
			
			std::vector<Scalar> b,z;
			try
			{
				b.resize(m_matrixSize);
				z.resize(m_matrixSize);
			}
			catch (const std::bad_alloc&)
			{
				//not enough memory
				return SquareMatrixTpl();
			}

			//init
			{
				for (unsigned ip=0; ip<m_matrixSize; ip++)
				{
					b[ip] = d[ip] = m_values[ip][ip]; //Initialize b and d to the diagonal of a.
					z[ip] = 0; //This vector will accumulate terms of the form tapq as in equation (11.1.14)
				}
			}

			//int j,iq,ip;
			//Scalar tresh,theta,tau,t,sm,s,h,g,c,;

			for (unsigned i=1; i<=maxIterationCount; i++)
			{
				//Sum off-diagonal elements
				Scalar sm = 0;
				{
					for (unsigned ip=0; ip<m_matrixSize-1; ip++)
					{
						for (unsigned iq = ip+1; iq<m_matrixSize; iq++)
							sm += fabs(m_values[ip][iq]);
					}
				}

				if (sm == 0) //The normal return, which relies on quadratic convergence to machine underflow.
				{
					//we only need the absolute values of eigenvalues
					for (unsigned ip=0; ip<m_matrixSize; ip++)
						d[ip] = fabs(d[ip]);

					return eigenVectors;
				}

				Scalar tresh = 0;
				if (i < 4)
					tresh = sm / static_cast<Scalar>(5*matrixSquareSize); //...on the first three sweeps.

				for (unsigned ip=0; ip<m_matrixSize-1; ip++)
				{
					for (unsigned iq=ip+1; iq<m_matrixSize; iq++)
					{
						Scalar g = fabs(m_values[ip][iq]) * 100;
						//After four sweeps, skip the rotation if the off-diagonal element is small.
						if (	i > 4
							&&	static_cast<float>(fabs(d[ip])+g) == static_cast<float>(fabs(d[ip]))
							&&	static_cast<float>(fabs(d[iq])+g) == static_cast<float>(fabs(d[iq])) )
						{
							m_values[ip][iq] = 0;
						}
						else if (fabs(m_values[ip][iq]) > tresh)
						{
							Scalar h = d[iq]-d[ip];
							Scalar t = 0;
							if (static_cast<float>(fabs(h)+g) == static_cast<float>(fabs(h)))
							{
								t = m_values[ip][iq]/h; //t = 1/(2�theta)
							}
							else
							{
								Scalar theta = h/(2*m_values[ip][iq]); //Equation (11.1.10).
								t = 1/(fabs(theta)+sqrt(1+theta*theta));
								if (theta < 0)
									t = -t;
							}

							Scalar c = 1/sqrt(t*t+1);
							Scalar s = t*c;
							Scalar tau = s/(1+c);
							h = t * m_values[ip][iq];
							z[ip] -= h;
							z[iq] += h;
							d[ip] -= h;
							d[iq] += h;
							m_values[ip][iq] = 0;

							//Case of rotations 1 <= j < p
							{
								for (unsigned j=0; j+1<=ip; j++)
									ROTATE(m_values,j,ip,j,iq)
							}
							//Case of rotations p < j < q
							{
								for (unsigned j=ip+1; j+1<=iq; j++)
									ROTATE(m_values,ip,j,j,iq)
							}
							//Case of rotations q < j <= n
							{
								for (unsigned j=iq+1; j<m_matrixSize; j++)
									ROTATE(m_values,ip,j,iq,j)
							}
							//Last case
							{
								for (unsigned j=0; j<m_matrixSize; j++)
									ROTATE(eigenVectors.m_values,j,ip,j,iq)
							}
						}

					}

				}

				//update b, d and z
				{
					for (unsigned ip=0; ip<m_matrixSize; ip++)
					{
						b[ip] += z[ip];
						d[ip] = b[ip];
						z[ip] = 0;
					}
				}
			}

			//Too many iterations!
			return SquareMatrixTpl();
		}

		//! Converts a 3*3 or 4*4 matrix to an OpenGL-style float matrix (float[16])
		void toGlMatrix(float M16f[]) const
		{
			assert(m_matrixSize == 3 || m_matrixSize == 4);
			memset(M16f,0,sizeof(float)*16);

			for (unsigned r=0; r<3; r++)
				for (unsigned c=0; c<3; c++)
					M16f[r+c*4] = static_cast<float>(m_values[r][c]);

			if (m_matrixSize == 4)
				for (unsigned r=0; r<3; r++)
				{
					M16f[12+r] = static_cast<float>(m_values[3][r]);
					M16f[3+r*4] = static_cast<float>(m_values[r][3]);
				}

			M16f[15] = 1.0f;
		}

		//! Converts a 3*3 or 4*4 matrix to an OpenGL-style double matrix (double[16])
		void toGlMatrix(double M16d[]) const
		{
			assert(m_matrixSize == 3 || m_matrixSize == 4);
			memset(M16d,0,sizeof(double)*16);

			for (unsigned r=0; r<3; r++)
				for (unsigned c=0; c<3; c++)
					M16d[r+c*4] = static_cast<double>(m_values[r][c]);

			if (m_matrixSize == 4)
			{
				for (unsigned r=0; r<3; r++)
				{
					M16d[12+r] = static_cast<double>(m_values[3][r]);
					M16d[3+r*4] = static_cast<double>(m_values[r][3]);
				}
			}

			M16d[15] = 1.0;
		}

		//! Sorts the eigenvectors in the decreasing order of their associated eigenvalues
		void sortEigenValuesAndVectors(bool absVal = false)
		{
			if (!eigenValues || m_matrixSize < 2)
				return;

			for (unsigned i=0; i<m_matrixSize-1; i++)
			{
				unsigned maxValIndex = i;
				for (unsigned j=i+1; j<m_matrixSize; j++)
					if (eigenValues[j] > eigenValues[maxValIndex]) //eigen values are always positive! (= square of singular values)
						maxValIndex = j;

				if (maxValIndex != i)
				{
					std::swap(eigenValues[i],eigenValues[maxValIndex]);
					for (unsigned j=0; j<m_matrixSize; j++)
						std::swap(m_values[j][i],m_values[j][maxValIndex]);
				}
			}
		}


		//! Returns the biggest eigenvalue and ist associated eigenvector
		/** \param maxEigenVector output vector to store max eigen vector
			\return max eigen value
		**/
		Scalar getMaxEigenValueAndVector(Scalar maxEigenVector[]) const
		{
			assert(eigenValues);

			unsigned maxIndex = 0;
			for (unsigned i=1; i<m_matrixSize; ++i)
				if (eigenValues[i] > eigenValues[maxIndex])
					maxIndex = i;

			return getEigenValueAndVector(maxIndex,maxEigenVector);
		}

		//! Returns the smallest eigenvalue and ist associated eigenvector
		/** \param minEigenVector output vector to store min eigen vector
			\return min eigen value
		**/
		Scalar getMinEigenValueAndVector(Scalar minEigenVector[]) const
		{
			assert(eigenValues);

			unsigned minIndex = 0;
			for (unsigned i=1; i<m_matrixSize; ++i)
				if (eigenValues[i] < eigenValues[minIndex])
					minIndex = i;

			return getEigenValueAndVector(minIndex,minEigenVector);
		}

		//! Returns the given eigenvalue and ist associated eigenvector
		/** \param index eigen requested value/vector index (< matrix size)
			\param eigenVector vector (size = matrix size) for output (or 0 if eigen vector is not requested)
			\return requested eigen value
		**/
		Scalar getEigenValueAndVector(unsigned index, Scalar* eigenVector = 0) const
		{
			assert(eigenValues);
			assert(index < m_matrixSize);

			if (eigenVector)
				for (unsigned i=0; i<m_matrixSize; ++i)
					eigenVector[i] = m_values[i][index];

			return eigenValues[index];
		}

		//! Returns the eigenvalues as an array
		inline const Scalar* getEigenValues() const { return eigenValues; }

	protected:

		//! Internal initialization
		/** \return initilization success
		**/
		bool init(unsigned size)
		{
			m_matrixSize = size;
			matrixSquareSize = m_matrixSize*m_matrixSize;

			m_values = 0;
			eigenValues = 0; //no eigeinvalues associated by default

			if (size != 0)
			{
				m_values = new Scalar*[m_matrixSize];
				if (m_values)
				{
					memset(m_values, 0, sizeof(Scalar*) * m_matrixSize);
					for (unsigned i=0; i<m_matrixSize; i++)
					{
						m_values[i] = new Scalar[m_matrixSize];
						if (m_values[i])
						{
							memset(m_values[i],0,sizeof(Scalar)*m_matrixSize);
						}
						else
						{
							//not enough memory!
							invalidate();
							return false;
						}
					}
				}
				else
				{
					//not enough memory!
					return false;
				}
			}

			return true;
		}

		//! Instantiates an array to store eigen values
		/** Warning: sets this array to zero, even if it was already instantiated!
		**/
		bool enableEigenValues()
		{
			if (!eigenValues && m_matrixSize != 0)
				eigenValues = new Scalar[m_matrixSize];

			return (eigenValues != 0);
		}

		//! Computes sub-matrix determinant
		double computeSubDet(Scalar** mat, unsigned matSize) const
		{
			if (matSize == 2)
			{
				return static_cast<double>(mat[0][0] * mat[1][1] - mat[0][1] * mat[1][0]);
			}

			Scalar** subMat = new Scalar*[matSize-1];
			if (subMat)
			{
				double subDet = 0;
				double sign = 1.0;

				for (unsigned row=0; row<matSize; row++)
				{
					unsigned k = 0;
					for (unsigned i=0; i<matSize; i++)
						if (i != row)
							subMat[k++] = mat[i]+1;

					subDet += static_cast<double>(mat[row][0]) * computeSubDet(subMat,matSize-1) * sign;
					sign = -sign;
				}

				delete[] subMat;
				return subDet;
			}
			else
			{
				//not enough memory
				return 0.0;
			}
		}

		//! Matrix size
		unsigned m_matrixSize;

		//! Matrix square-size
		unsigned matrixSquareSize;

		//! Eigenvalues
		/** Typically associated to an eigen matrix!
		**/
		Scalar* eigenValues;
	};

	//! Default CC square matrix type (PointCoordinateType)
	typedef SquareMatrixTpl<PointCoordinateType> SquareMatrix;

	//! Float square matrix type
	typedef SquareMatrixTpl<float> SquareMatrixf;

	//! Double square matrix type
	typedef SquareMatrixTpl<double> SquareMatrixd;

} //namespace CCLib

#endif //SQUARE_MATRIX_HEADER
