using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;

using System.IO;

namespace ParticleDataSetAdjuster
{
    class Program
    {
        static void Main(string[] args)
        {
            Dictionary<string, List<string>> particleValues = new Dictionary<string, List<string>>();

            StreamReader dataFile = new StreamReader("alphalog.txt");
            char[] split = {','};
            while(!dataFile.EndOfStream)
            {
                string line = dataFile.ReadLine();
                string[] parts = line.Split(split);
                
                List<string> values;
                if(particleValues.TryGetValue(parts[0], out values))
                {
                    values.Add(parts[1]);
                }
                else
                {
                    values = new List<string>();
                    values.Add(parts[1]);
                    particleValues.Add(parts[0], values);
                }
            }

            int maxLen = 0;
            foreach (KeyValuePair<string, List<string>> values in particleValues)
            {
                maxLen = Math.Max(maxLen, values.Value.Count);
            }

            foreach (KeyValuePair<string, List<string>> values in particleValues)
            {
                if(values.Value.Count < maxLen)
                {
                    int padAmount = maxLen - values.Value.Count;
                    for(int i = 0; i < padAmount; ++i)
                    {
                        values.Value.Add("0.0");
                    }
                }
            }

            StreamWriter output = new StreamWriter("converted.txt");
            foreach (KeyValuePair<string, List<string>> values in particleValues)
            {
                StringBuilder data = new StringBuilder();
                data.Append(values.Key);
                foreach(string val in values.Value)
                {
                    data.Append(",");
                    data.Append(val);   
                }
                output.WriteLine(data);
            }
        }
    }
}
